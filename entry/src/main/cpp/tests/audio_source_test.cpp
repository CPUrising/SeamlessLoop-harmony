/*
 * AudioSource 单测：覆盖 P04（虚拟轴总长）、P05（A 播完接 B）、P07（跨文件 seek）。
 *
 * 使用合成 A/B fixture：A = 33075 帧、B = 88200 帧，虚拟轴总长 121275 帧。
 * 只使用 fixtures/synthetic/ 下的合成音频。
 */

#include "audio_source.h"
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

constexpr int64_t kAFrames = 33075;
constexpr int64_t kBFrames = 88200;
constexpr int64_t kTotalFrames = kAFrames + kBFrames;

// P04：虚拟轴总长等于两文件之和，采样率与声道数取 A 的值。
TEST_CASE(testPairReportsVirtualTotal)
{
    slh::AudioSource src;
    CHECK_TRUE(src.OpenPair(FixturePath("ab_pair_44100_stereo_a.wav").c_str(),
                            FixturePath("ab_pair_44100_stereo_b.wav").c_str()));
    CHECK_TRUE(src.IsOpen());
    CHECK_TRUE(src.IsPair());

    const slh::AudioFormatInfo& info = src.Format();
    CHECK_EQ(info.sampleRate, 44100);
    CHECK_EQ(info.channels, 2);
    CHECK_EQ(info.totalFrames, kTotalFrames);
    CHECK_EQ(info.bytesPerFrame(), 4);
    CHECK_EQ(src.CurrentFrame(), static_cast<int64_t>(0));
}

// 单文件模式：总长等于该文件帧数，IsPair 为 false。
TEST_CASE(testSingleFileMode)
{
    slh::AudioSource src;
    CHECK_TRUE(src.Open(FixturePath("intro_loop_44100_mono.wav").c_str()));
    CHECK_TRUE(src.IsOpen());
    CHECK_FALSE(src.IsPair());
    CHECK_EQ(src.Format().totalFrames, static_cast<int64_t>(121275));
    CHECK_EQ(src.Format().channels, 1);
}

// P05：读到 A 末尾后必须自动接 B 的第 0 帧，不返回 0。
TEST_CASE(testCrossFileReadIsContinuous)
{
    slh::AudioSource src;
    CHECK_TRUE(src.OpenPair(FixturePath("ab_pair_44100_stereo_a.wav").c_str(),
                            FixturePath("ab_pair_44100_stereo_b.wav").c_str()));

    // 从 A 的最后一帧开始读 4 帧：应得到 A 末帧 + B 的前 3 帧。
    CHECK_EQ(src.SeekToFrame(kAFrames - 1), kAFrames - 1);

    std::vector<int16_t> buf(4 * 2);
    CHECK_EQ(src.ReadFrames(buf.data(), 4), static_cast<int64_t>(4));
    // 虚拟轴位置应推进到 A 末帧之后 3 帧，即 NA + 3。
    CHECK_EQ(src.CurrentFrame(), kAFrames + 3);
}

// P05：一次读取跨越整个 A→B 边界，返回帧数必须等于请求帧数。
TEST_CASE(testReadAcrossBoundaryReturnsFullCount)
{
    slh::AudioSource src;
    CHECK_TRUE(src.OpenPair(FixturePath("ab_pair_44100_stereo_a.wav").c_str(),
                            FixturePath("ab_pair_44100_stereo_b.wav").c_str()));

    // 从 A 末尾前 100 帧开始读 200 帧，必然跨越边界。
    CHECK_EQ(src.SeekToFrame(kAFrames - 100), kAFrames - 100);
    std::vector<int16_t> buf(200 * 2);
    CHECK_EQ(src.ReadFrames(buf.data(), 200), static_cast<int64_t>(200));
    CHECK_EQ(src.CurrentFrame(), kAFrames + 100);
}

// P07：p < NA 时在 A 内 seek。
TEST_CASE(testSeekWithinA)
{
    slh::AudioSource src;
    CHECK_TRUE(src.OpenPair(FixturePath("ab_pair_44100_stereo_a.wav").c_str(),
                            FixturePath("ab_pair_44100_stereo_b.wav").c_str()));

    CHECK_EQ(src.SeekToFrame(1000), static_cast<int64_t>(1000));
    CHECK_EQ(src.CurrentFrame(), static_cast<int64_t>(1000));

    std::vector<int16_t> buf(16 * 2);
    CHECK_EQ(src.ReadFrames(buf.data(), 16), static_cast<int64_t>(16));
    CHECK_EQ(src.CurrentFrame(), static_cast<int64_t>(1016));
}

// P07：p >= NA 时在 B 内 seek 到 p - NA。
TEST_CASE(testSeekIntoB)
{
    slh::AudioSource src;
    CHECK_TRUE(src.OpenPair(FixturePath("ab_pair_44100_stereo_a.wav").c_str(),
                            FixturePath("ab_pair_44100_stereo_b.wav").c_str()));

    CHECK_EQ(src.SeekToFrame(kAFrames + 500), kAFrames + 500);
    CHECK_EQ(src.CurrentFrame(), kAFrames + 500);

    std::vector<int16_t> buf(16 * 2);
    CHECK_EQ(src.ReadFrames(buf.data(), 16), static_cast<int64_t>(16));
    CHECK_EQ(src.CurrentFrame(), kAFrames + 516);
}

// 三个边界：p = NA-1、p = NA、p = NA+NB-1。
TEST_CASE(testBoundaryPositions)
{
    slh::AudioSource src;
    CHECK_TRUE(src.OpenPair(FixturePath("ab_pair_44100_stereo_a.wav").c_str(),
                            FixturePath("ab_pair_44100_stereo_b.wav").c_str()));

    // p = NA - 1：A 的最后一帧，读 1 帧后位置到 NA。
    CHECK_EQ(src.SeekToFrame(kAFrames - 1), kAFrames - 1);
    std::vector<int16_t> one(2);
    CHECK_EQ(src.ReadFrames(one.data(), 1), static_cast<int64_t>(1));
    CHECK_EQ(src.CurrentFrame(), kAFrames);

    // p = NA：B 的第 0 帧。
    CHECK_EQ(src.SeekToFrame(kAFrames), kAFrames);
    CHECK_EQ(src.ReadFrames(one.data(), 1), static_cast<int64_t>(1));
    CHECK_EQ(src.CurrentFrame(), kAFrames + 1);

    // p = NA + NB - 1：虚拟轴最后一帧，读 1 帧后位置到总长。
    CHECK_EQ(src.SeekToFrame(kTotalFrames - 1), kTotalFrames - 1);
    CHECK_EQ(src.ReadFrames(one.data(), 1), static_cast<int64_t>(1));
    CHECK_EQ(src.CurrentFrame(), kTotalFrames);
}

// 虚拟轴末尾之后必须返回 0。
TEST_CASE(testReadStopsAtVirtualEnd)
{
    slh::AudioSource src;
    CHECK_TRUE(src.OpenPair(FixturePath("ab_pair_44100_stereo_a.wav").c_str(),
                            FixturePath("ab_pair_44100_stereo_b.wav").c_str()));

    CHECK_EQ(src.SeekToFrame(kTotalFrames), kTotalFrames);
    std::vector<int16_t> buf(16 * 2);
    CHECK_EQ(src.ReadFrames(buf.data(), 16), static_cast<int64_t>(0));
}

// seek 越界必须 clamp 到 [0, totalFrames]。
TEST_CASE(testSeekClampsOutOfRange)
{
    slh::AudioSource src;
    CHECK_TRUE(src.OpenPair(FixturePath("ab_pair_44100_stereo_a.wav").c_str(),
                            FixturePath("ab_pair_44100_stereo_b.wav").c_str()));

    CHECK_EQ(src.SeekToFrame(-100), static_cast<int64_t>(0));
    CHECK_EQ(src.CurrentFrame(), static_cast<int64_t>(0));

    CHECK_EQ(src.SeekToFrame(kTotalFrames + 999999), kTotalFrames);
    CHECK_EQ(src.CurrentFrame(), kTotalFrames);
}

// 跨文件 seek 后读出的数据必须来自 B，而不是 A。
TEST_CASE(testSeekIntoBReturnsBData)
{
    slh::AudioSource src;
    CHECK_TRUE(src.OpenPair(FixturePath("ab_pair_44100_stereo_a.wav").c_str(),
                            FixturePath("ab_pair_44100_stereo_b.wav").c_str()));

    // 直接读 B 的第 0 帧。
    CHECK_EQ(src.SeekToFrame(kAFrames), kAFrames);
    std::vector<int16_t> fromB(64 * 2);
    CHECK_EQ(src.ReadFrames(fromB.data(), 64), static_cast<int64_t>(64));

    // 用独立解码器读 B 文件开头，两者必须逐字节一致。
    slh::AudioDecoder bOnly;
    CHECK_TRUE(bOnly.Open(FixturePath("ab_pair_44100_stereo_b.wav").c_str()));
    std::vector<int16_t> expected(64 * 2);
    CHECK_EQ(bOnly.ReadFrames(expected.data(), 64), static_cast<int64_t>(64));

    bool identical = true;
    for (size_t i = 0; i < expected.size(); ++i) {
        if (fromB[i] != expected[i]) {
            identical = false;
            break;
        }
    }
    CHECK_TRUE(identical);
}

// 采样率不一致的两个文件不得配对。
TEST_CASE(testMismatchedFormatsRejected)
{
    slh::AudioSource src;
    // A 是 44.1kHz 立体声，另一个是 48kHz 立体声。
    CHECK_FALSE(src.OpenPair(FixturePath("ab_pair_44100_stereo_a.wav").c_str(),
                             FixturePath("intro_loop_48000_stereo.wav").c_str()));
    CHECK_FALSE(src.IsOpen());
}

// 未打开时各接口必须安全返回。
TEST_CASE(testUnopenedSourceIsSafe)
{
    slh::AudioSource src;
    CHECK_FALSE(src.IsOpen());
    CHECK_FALSE(src.IsPair());
    CHECK_EQ(src.SeekToFrame(0), static_cast<int64_t>(-1));
    std::vector<int16_t> buf(8);
    CHECK_EQ(src.ReadFrames(buf.data(), 4), static_cast<int64_t>(0));
    CHECK_EQ(src.CurrentFrame(), static_cast<int64_t>(0));
}

} // namespace

int main()
{
    std::printf("== AudioSourceTests ==\n");
    std::printf("  fixture dir: %s\n", FIXTURE_DIR);
    testPairReportsVirtualTotal_runner();
    testSingleFileMode_runner();
    testCrossFileReadIsContinuous_runner();
    testReadAcrossBoundaryReturnsFullCount_runner();
    testSeekWithinA_runner();
    testSeekIntoB_runner();
    testBoundaryPositions_runner();
    testReadStopsAtVirtualEnd_runner();
    testSeekClampsOutOfRange_runner();
    testSeekIntoBReturnsBData_runner();
    testMismatchedFormatsRejected_runner();
    testUnopenedSourceIsSafe_runner();
    return Summarize("AudioSourceTests");
}
