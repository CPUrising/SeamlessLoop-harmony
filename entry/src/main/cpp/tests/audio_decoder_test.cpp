/*
 * AudioDecoder 单测：覆盖 T12（加载后报告格式）与 T14 的解码侧前置
 * （按采样点 seek 与 clamp）。
 *
 * 只使用 fixtures/synthetic/ 下的合成音频，不涉及任何真实音乐文件。
 * fixture 目录通过 CMake 的 FIXTURE_DIR 宏注入，避免测试里写死绝对路径。
 */

#include "audio_decoder.h"
#include "test_common.h"

#include <cstdint>
#include <string>
#include <vector>

#ifndef FIXTURE_DIR
#error "FIXTURE_DIR 未定义：请在 CMake 中通过 -DFIXTURE_DIR=... 注入"
#endif

namespace {

std::string FixturePath(const char* name)
{
    std::string p = FIXTURE_DIR;
    if (!p.empty() && p.back() != '/' && p.back() != '\\') {
        p += '/';
    }
    p += name;
    return p;
}

// T12：加载 WAV 后必须报出采样率、声道数、总采样数。
TEST_CASE(testLoadReportsFormatWav)
{
    slh::AudioDecoder dec;
    const std::string path = FixturePath("intro_loop_44100_mono.wav");
    CHECK_TRUE(dec.Open(path.c_str()));
    CHECK_TRUE(dec.IsOpen());

    const slh::AudioFormatInfo& info = dec.Format();
    CHECK_TRUE(info.format == slh::AudioFormat::kWav);
    CHECK_EQ(info.sampleRate, 44100);
    CHECK_EQ(info.channels, 1);
    CHECK_EQ(info.totalFrames, static_cast<int64_t>(121275));
    // BlockAlign = 声道数 * 每采样字节数（S16）
    CHECK_EQ(info.bytesPerFrame(), 2);
    // 播放必须从采样点 0 开始，不直接跳到 LoopStart（T12 后半句）。
    CHECK_EQ(dec.CurrentFrame(), static_cast<int64_t>(0));
}

TEST_CASE(testLoadReportsFormatWavStereo)
{
    slh::AudioDecoder dec;
    const std::string path = FixturePath("intro_loop_48000_stereo.wav");
    CHECK_TRUE(dec.Open(path.c_str()));

    const slh::AudioFormatInfo& info = dec.Format();
    CHECK_EQ(info.sampleRate, 48000);
    CHECK_EQ(info.channels, 2);
    CHECK_EQ(info.totalFrames, static_cast<int64_t>(132000));
    CHECK_EQ(info.bytesPerFrame(), 4);
}

// MP3 的总帧数由 dr_mp3 扣除编码器延迟/填充后给出，与 WAV 不同。
TEST_CASE(testLoadReportsFormatMp3)
{
    slh::AudioDecoder dec;
    const std::string path = FixturePath("intro_loop_44100_mono.mp3");
    CHECK_TRUE(dec.Open(path.c_str()));

    const slh::AudioFormatInfo& info = dec.Format();
    CHECK_TRUE(info.format == slh::AudioFormat::kMp3);
    CHECK_EQ(info.sampleRate, 44100);
    CHECK_EQ(info.channels, 1);
    // 2.75 秒 @44.1kHz ≈ 121275 帧；允许编码器延迟带来的少量偏差。
    CHECK_TRUE(info.totalFrames > 0);
    CHECK_TRUE(info.totalFrames > 118000 && info.totalFrames < 124000);
}

// T14：seek 到合法位置后，CurrentFrame 必须等于目标值。
TEST_CASE(testSeekToFrameWithinRange)
{
    slh::AudioDecoder dec;
    CHECK_TRUE(dec.Open(FixturePath("intro_loop_44100_mono.wav").c_str()));

    const int64_t target = 33075; // manifest 里的 LoopStart
    CHECK_EQ(dec.SeekToFrame(target), target);
    CHECK_EQ(dec.CurrentFrame(), target);
}

// T14：负值与超界值必须 clamp 到 [0, totalFrames]。
TEST_CASE(testSeekClampsOutOfRange)
{
    slh::AudioDecoder dec;
    CHECK_TRUE(dec.Open(FixturePath("intro_loop_44100_mono.wav").c_str()));
    const int64_t total = dec.Format().totalFrames;

    CHECK_EQ(dec.SeekToFrame(-1), static_cast<int64_t>(0));
    CHECK_EQ(dec.CurrentFrame(), static_cast<int64_t>(0));

    CHECK_EQ(dec.SeekToFrame(total + 999999), total);
    CHECK_EQ(dec.CurrentFrame(), total);
}

// seek 后读出的数据必须是该位置之后的数据，而不是文件头。
TEST_CASE(testReadAfterSeekReturnsLaterData)
{
    slh::AudioDecoder dec;
    CHECK_TRUE(dec.Open(FixturePath("intro_loop_44100_mono.wav").c_str()));

    // 先读文件头 1024 帧
    std::vector<int16_t> head(1024);
    CHECK_EQ(dec.ReadFrames(head.data(), 1024), static_cast<int64_t>(1024));

    // 再 seek 到 LoopStart，读 1024 帧
    const int64_t target = 33075;
    CHECK_EQ(dec.SeekToFrame(target), target);
    std::vector<int16_t> mid(1024);
    CHECK_EQ(dec.ReadFrames(mid.data(), 1024), static_cast<int64_t>(1024));
    CHECK_EQ(dec.CurrentFrame(), target + 1024);

    // 两段数据应当不同（合成波形是变化的，不是常量）。
    bool identical = true;
    for (size_t i = 0; i < head.size(); ++i) {
        if (head[i] != mid[i]) {
            identical = false;
            break;
        }
    }
    CHECK_FALSE(identical);
}

// 读到文件末尾后必须返回 0，且不越界。
TEST_CASE(testReadStopsAtEnd)
{
    slh::AudioDecoder dec;
    CHECK_TRUE(dec.Open(FixturePath("intro_loop_44100_mono.wav").c_str()));
    const int64_t total = dec.Format().totalFrames;

    CHECK_EQ(dec.SeekToFrame(total), total);
    std::vector<int16_t> buf(256);
    CHECK_EQ(dec.ReadFrames(buf.data(), 256), static_cast<int64_t>(0));
    CHECK_EQ(dec.CurrentFrame(), total);
}

// 不支持的扩展名必须明确失败，不猜测格式。
TEST_CASE(testOpenRejectsUnsupportedExtension)
{
    slh::AudioDecoder dec;
    CHECK_FALSE(dec.Open(FixturePath("manifest.json").c_str()));
    CHECK_FALSE(dec.IsOpen());
    CHECK_FALSE(dec.Open(nullptr));
    CHECK_FALSE(dec.Open(""));
}

// 未打开时 seek / read 必须安全失败。
TEST_CASE(testOperationsOnClosedDecoderAreSafe)
{
    slh::AudioDecoder dec;
    CHECK_FALSE(dec.IsOpen());
    CHECK_EQ(dec.SeekToFrame(100), static_cast<int64_t>(-1));
    int16_t buf[4] = {0};
    CHECK_EQ(dec.ReadFrames(buf, 4), static_cast<int64_t>(0));
}

// Close 后可再次 Open，格式信息不残留。
TEST_CASE(testReopenAfterClose)
{
    slh::AudioDecoder dec;
    CHECK_TRUE(dec.Open(FixturePath("intro_loop_44100_mono.wav").c_str()));
    CHECK_EQ(dec.Format().channels, 1);
    dec.Close();
    CHECK_FALSE(dec.IsOpen());

    CHECK_TRUE(dec.Open(FixturePath("intro_loop_48000_stereo.wav").c_str()));
    CHECK_EQ(dec.Format().channels, 2);
    CHECK_EQ(dec.Format().sampleRate, 48000);
}

} // namespace

int main()
{
    std::printf("== DecoderTests ==\n");
    std::printf("  fixture dir: %s\n", FIXTURE_DIR);
    testLoadReportsFormatWav_runner();
    testLoadReportsFormatWavStereo_runner();
    testLoadReportsFormatMp3_runner();
    testSeekToFrameWithinRange_runner();
    testSeekClampsOutOfRange_runner();
    testReadAfterSeekReturnsLaterData_runner();
    testReadStopsAtEnd_runner();
    testOpenRejectsUnsupportedExtension_runner();
    testOperationsOnClosedDecoderAreSafe_runner();
    testReopenAfterClose_runner();
    return Summarize("DecoderTests");
}
