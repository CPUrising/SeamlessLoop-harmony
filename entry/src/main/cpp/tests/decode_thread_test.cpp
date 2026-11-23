/*
 * DecodeThread 单测：验证「持续解码 → 写入 FIFO → 到末尾停止」与「循环跳转」。
 *
 * 用真实合成 fixture 驱动真实解码器，因此这条链路是端到端可判定的，
 * 不依赖 OHAudio 与真机。
 */

#include "decode_thread.h"
#include "test_common.h"

#include <chrono>
#include <cstring>
#include <string>
#include <thread>
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

// 等待条件成立，最多 timeoutMs。避免用固定 sleep 造成偶发失败。
template <typename Pred>
bool WaitUntil(Pred pred, int timeoutMs = 5000)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return pred();
}

// 消费者持续排空 FIFO，模拟 OHAudio 回调线程。
class Drainer {
public:
    explicit Drainer(slh::RingBuffer* ring) : ring_(ring), thread_([this] { Loop(); }) {}

    ~Drainer() { Stop(); }

    void Stop()
    {
        stop_.store(true);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    size_t TotalRead() const { return totalRead_.load(); }

private:
    void Loop()
    {
        std::vector<uint8_t> buf(4096);
        while (!stop_.load()) {
            const size_t n = ring_->Read(buf.data(), buf.size());
            if (n == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            totalRead_.fetch_add(n);
        }
    }

    slh::RingBuffer* ring_;
    std::atomic<bool> stop_{false};
    std::atomic<size_t> totalRead_{0};
    std::thread thread_;
};

// 消费者：排空 FIFO 并保留全部字节，用于逐帧比对循环拼接结果。
// 仅在 Stop() 之后读取 Data()，避免与消费线程竞争。
class CaptureSink {
public:
    explicit CaptureSink(slh::RingBuffer* ring) : ring_(ring), thread_([this] { Loop(); }) {}

    ~CaptureSink() { Stop(); }

    void Stop()
    {
        stop_.store(true);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    size_t Bytes() const { return bytes_.load(); }

    const std::vector<uint8_t>& Data() const { return data_; }

private:
    void Loop()
    {
        std::vector<uint8_t> buf(8192);
        while (!stop_.load()) {
            const size_t n = ring_->Read(buf.data(), buf.size());
            if (n == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            Append(buf.data(), n);
        }
        // 停止后把 FIFO 里剩余数据也收干净，便于精确比对。
        size_t n = 0;
        while ((n = ring_->Read(buf.data(), buf.size())) > 0) {
            Append(buf.data(), n);
        }
    }

    void Append(const uint8_t* p, size_t n)
    {
        data_.insert(data_.end(), p, p + n);
        bytes_.fetch_add(n);
    }

    slh::RingBuffer* ring_;
    std::atomic<bool> stop_{false};
    std::atomic<size_t> bytes_{0};
    std::vector<uint8_t> data_;
    std::thread thread_;
};

// 用独立解码器把整个文件解成 S16 交错 PCM，作为循环拼接的参考数据。
std::vector<int16_t> ReadWholeFile(const std::string& path, int64_t* totalOut)
{
    std::vector<int16_t> data;
    slh::AudioSource dec;
    if (!dec.Open(path.c_str())) {
        return data;
    }
    const int64_t total = dec.Format().totalFrames;
    const int32_t channels = dec.Format().channels;
    data.resize(static_cast<size_t>(total * channels));
    if (dec.ReadFrames(data.data(), total) != total) {
        data.clear();
        return data;
    }
    if (totalOut != nullptr) {
        *totalOut = total;
    }
    return data;
}

// 启动后必须真的把数据解码进 FIFO。
TEST_CASE(testDecodesIntoRingBuffer)
{
    slh::AudioSource dec;
    CHECK_TRUE(dec.Open(FixturePath("intro_loop_44100_mono.wav").c_str()));
    slh::RingBuffer ring(44100 * 1 * 2 * 2); // 2 秒

    slh::DecodeThread dt(&dec, &ring);
    CHECK_TRUE(dt.Start());
    CHECK_TRUE(dt.IsRunning());

    CHECK_TRUE(WaitUntil([&] { return ring.AvailableToRead() > 0; }, 3000));
    CHECK_TRUE(dt.DecodedFrames() > 0);

    dt.Stop();
    CHECK_FALSE(dt.IsRunning());
}

// 完整播完：解码帧数必须等于文件总帧数，且标记到达末尾。
TEST_CASE(testDecodesAllFramesAndReachesEnd)
{
    slh::AudioSource dec;
    CHECK_TRUE(dec.Open(FixturePath("intro_loop_44100_mono.wav").c_str()));
    const int64_t total = dec.Format().totalFrames;

    slh::RingBuffer ring(44100 * 2 * 2);
    Drainer drainer(&ring);

    slh::DecodeThread dt(&dec, &ring, 2048);
    CHECK_TRUE(dt.Start());

    CHECK_TRUE(WaitUntil([&] { return dt.ReachedEnd(); }, 8000));
    dt.Stop();

    CHECK_EQ(dt.DecodedFrames(), total);
    // 排空后，消费者读到的字节数应等于解码写入的字节数。
    CHECK_TRUE(WaitUntil([&] { return drainer.TotalRead() == static_cast<size_t>(total * 2); }, 3000));
    CHECK_EQ(drainer.TotalRead(), static_cast<size_t>(total * 2));
}

// FIFO 满时解码线程必须等待，不能丢数据也不能忙转。
TEST_CASE(testWaitsWhenRingBufferFull)
{
    slh::AudioSource dec;
    CHECK_TRUE(dec.Open(FixturePath("intro_loop_44100_mono.wav").c_str()));

    // 容量故意设得很小：只够 64 帧。
    const size_t capBytes = 64 * 2;
    slh::RingBuffer ring(capBytes);

    slh::DecodeThread dt(&dec, &ring, 4096);
    CHECK_TRUE(dt.Start());

    // 给足时间让它写满并卡住。
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // FIFO 不能被写爆。
    CHECK_EQ(ring.AvailableToRead(), capBytes);
    CHECK_EQ(ring.AvailableToWrite(), static_cast<size_t>(0));
    // 线程应仍然活着（在等待空间），而不是已结束。
    CHECK_TRUE(dt.IsRunning());
    CHECK_FALSE(dt.ReachedEnd());

    dt.Stop();
}

// 停止后不再写入新数据。
TEST_CASE(testStopHaltsDecoding)
{
    slh::AudioSource dec;
    CHECK_TRUE(dec.Open(FixturePath("intro_loop_44100_mono.wav").c_str()));
    slh::RingBuffer ring(44100 * 2 * 2);
    Drainer drainer(&ring);

    slh::DecodeThread dt(&dec, &ring, 1024);
    CHECK_TRUE(dt.Start());
    CHECK_TRUE(WaitUntil([&] { return dt.DecodedFrames() > 0; }, 3000));

    dt.Stop();
    CHECK_FALSE(dt.IsRunning());

    const int64_t afterStop = dt.DecodedFrames();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK_EQ(dt.DecodedFrames(), afterStop);
}

// 未打开解码器时 Start 必须失败，不能启动野线程。
TEST_CASE(testStartFailsWhenDecoderClosed)
{
    slh::AudioSource dec;
    slh::RingBuffer ring(1024);
    slh::DecodeThread dt(&dec, &ring);
    CHECK_FALSE(dt.Start());
    CHECK_FALSE(dt.IsRunning());
    dt.Stop(); // 必须可安全重复调用
}

// 空指针防护。
TEST_CASE(testStartFailsOnNullPointers)
{
    slh::AudioSource dec;
    CHECK_TRUE(dec.Open(FixturePath("intro_loop_44100_mono.wav").c_str()));
    slh::RingBuffer ring(1024);

    slh::DecodeThread noDecoder(nullptr, &ring);
    CHECK_FALSE(noDecoder.Start());

    slh::DecodeThread noRing(&dec, nullptr);
    CHECK_FALSE(noRing.Start());
}

// 立体声文件：写入字节数应为 帧数 × 4。
TEST_CASE(testStereoWritesCorrectByteCount)
{
    slh::AudioSource dec;
    CHECK_TRUE(dec.Open(FixturePath("intro_loop_48000_stereo.wav").c_str()));
    const int64_t total = dec.Format().totalFrames;

    slh::RingBuffer ring(48000 * 4 * 2);
    Drainer drainer(&ring);

    slh::DecodeThread dt(&dec, &ring, 2048);
    CHECK_TRUE(dt.Start());
    CHECK_TRUE(WaitUntil([&] { return dt.ReachedEnd(); }, 8000));
    dt.Stop();

    CHECK_EQ(dt.DecodedFrames(), total);
    CHECK_TRUE(WaitUntil([&] { return drainer.TotalRead() == static_cast<size_t>(total * 4); }, 3000));
    CHECK_EQ(drainer.TotalRead(), static_cast<size_t>(total * 4));
}

// T01：到达 LoopEnd 时跳回 LoopStart，输出必须等于「intro + 循环体重复」。
TEST_CASE(testLoopJumpsBackToStart)
{
    const std::string path = FixturePath("intro_loop_44100_mono.wav");
    int64_t total = 0;
    const std::vector<int16_t> all = ReadWholeFile(path, &total);
    CHECK_EQ(total, static_cast<int64_t>(121275));
    CHECK_EQ(all.size(), static_cast<size_t>(total));

    const int64_t loopStart = 33075;
    const int64_t loopEnd = total;
    const int64_t loopLen = loopEnd - loopStart;
    const int64_t cycles = 2;
    const int64_t expectFrames = loopEnd + loopLen * cycles;

    slh::AudioSource dec;
    CHECK_TRUE(dec.Open(path.c_str()));
    slh::RingBuffer ring(44100 * 2); // 1 秒
    CaptureSink sink(&ring);

    slh::DecodeThread dt(&dec, &ring, 4096);
    CHECK_TRUE(dt.SetLoopRange(loopStart, loopEnd));
    dt.SetLooping(true);
    CHECK_TRUE(dt.IsLooping());
    CHECK_TRUE(dt.Start());

    CHECK_TRUE(WaitUntil([&] { return sink.Bytes() >= static_cast<size_t>(expectFrames * 2); }, 15000));
    dt.Stop();
    sink.Stop();

    const std::vector<uint8_t>& out = sink.Data();
    CHECK_TRUE(out.size() >= static_cast<size_t>(expectFrames * 2));
    // 第一段：文件开头到 LoopEnd，与原始 PCM 完全一致。
    CHECK_EQ(std::memcmp(out.data(), all.data(), static_cast<size_t>(loopEnd) * 2), 0);
    // 之后每一段：都必须是 [LoopStart, LoopEnd) 的完整副本。
    for (int64_t i = 0; i < cycles; ++i) {
        const int64_t offsetFrames = loopEnd + i * loopLen;
        CHECK_EQ(std::memcmp(out.data() + offsetFrames * 2,
                             all.data() + loopStart,
                             static_cast<size_t>(loopLen) * 2), 0);
    }

    // 循环期间不得判定为播放结束；解码器必须始终打开（未重开文件）。
    CHECK_FALSE(dt.ReachedEnd());
    CHECK_TRUE(dec.IsOpen());
    CHECK_EQ(dec.Format().totalFrames, total);
}

// 循环体短于 chunkFrames：每次只读到 LoopEnd 就跳转，不得多读越过边界。
TEST_CASE(testShortLoopRangeRepeatsBody)
{
    const std::string path = FixturePath("intro_loop_44100_mono.wav");
    int64_t total = 0;
    const std::vector<int16_t> all = ReadWholeFile(path, &total);

    const int64_t loopStart = 33075;
    const int64_t loopEnd = 34075; // 仅 1000 帧，远小于 chunk 4096
    const int64_t loopLen = loopEnd - loopStart;
    const int64_t cycles = 3;
    const int64_t expectFrames = loopEnd + loopLen * cycles;

    slh::AudioSource dec;
    CHECK_TRUE(dec.Open(path.c_str()));
    slh::RingBuffer ring(44100 * 2);
    CaptureSink sink(&ring);

    slh::DecodeThread dt(&dec, &ring, 4096);
    CHECK_TRUE(dt.SetLoopRange(loopStart, loopEnd));
    dt.SetLooping(true);
    CHECK_TRUE(dt.Start());

    CHECK_TRUE(WaitUntil([&] { return sink.Bytes() >= static_cast<size_t>(expectFrames * 2); }, 15000));
    dt.Stop();
    sink.Stop();

    const std::vector<uint8_t>& out = sink.Data();
    CHECK_EQ(std::memcmp(out.data(), all.data(), static_cast<size_t>(loopEnd) * 2), 0);
    for (int64_t i = 0; i < cycles; ++i) {
        const int64_t offsetFrames = loopEnd + i * loopLen;
        CHECK_EQ(std::memcmp(out.data() + offsetFrames * 2,
                             all.data() + loopStart,
                             static_cast<size_t>(loopLen) * 2), 0);
    }
}

// 立体声（BlockAlign = 4）：循环拼接后仍是整帧数据，字节偏移按 4 对齐。
TEST_CASE(testStereoLoopKeepsFrameAlignment)
{
    const std::string path = FixturePath("intro_loop_48000_stereo.wav");
    int64_t total = 0;
    const std::vector<int16_t> all = ReadWholeFile(path, &total);
    CHECK_EQ(total, static_cast<int64_t>(132000));

    const int64_t blockAlign = 4;
    const int64_t loopStart = 36000;
    const int64_t loopEnd = total;
    const int64_t loopLen = loopEnd - loopStart;

    slh::AudioSource dec;
    CHECK_TRUE(dec.Open(path.c_str()));
    slh::RingBuffer ring(48000 * blockAlign);
    CaptureSink sink(&ring);

    slh::DecodeThread dt(&dec, &ring, 2048);
    CHECK_TRUE(dt.SetLoopRange(loopStart, loopEnd));
    dt.SetLooping(true);
    CHECK_TRUE(dt.Start());

    const int64_t expectBytes = (loopEnd + loopLen) * blockAlign;
    CHECK_TRUE(WaitUntil([&] { return sink.Bytes() >= static_cast<size_t>(expectBytes); }, 15000));
    dt.Stop();
    sink.Stop();

    const std::vector<uint8_t>& out = sink.Data();
    // 拼接边界必须落在整帧上：偏移量都是 BlockAlign 的整数倍。
    CHECK_EQ((loopEnd * blockAlign) % blockAlign, static_cast<int64_t>(0));
    CHECK_EQ((loopStart * blockAlign) % blockAlign, static_cast<int64_t>(0));
    CHECK_EQ(std::memcmp(out.data(), all.data(), static_cast<size_t>(loopEnd) * blockAlign), 0);
    // 参考数据是 int16 交错数组，偏移必须按「帧号 × 声道数」换算，不能直接用帧号。
    CHECK_EQ(std::memcmp(out.data() + loopEnd * blockAlign,
                         all.data() + loopStart * 2,
                         static_cast<size_t>(loopLen) * blockAlign), 0);
}

// 循环区间校验：越界与退化一律拒绝；未设置区间时开循环无效。
TEST_CASE(testLoopRangeValidation)
{
    slh::AudioSource dec;
    CHECK_TRUE(dec.Open(FixturePath("intro_loop_44100_mono.wav").c_str()));
    const int64_t total = dec.Format().totalFrames;
    slh::RingBuffer ring(1024);
    slh::DecodeThread dt(&dec, &ring);

    // 未设置区间就开循环必须无效，否则解码线程会在 LoopEnd 原地打转。
    dt.SetLooping(true);
    CHECK_FALSE(dt.IsLooping());

    CHECK_FALSE(dt.SetLoopRange(-1, 100));
    CHECK_FALSE(dt.SetLoopRange(100, 100));
    CHECK_FALSE(dt.SetLoopRange(200, 100));
    CHECK_FALSE(dt.SetLoopRange(0, total + 1));
    CHECK_FALSE(dt.IsLooping());

    CHECK_TRUE(dt.SetLoopRange(0, total));
    dt.SetLooping(true);
    CHECK_TRUE(dt.IsLooping());
    dt.SetLooping(false);
    CHECK_FALSE(dt.IsLooping());

    // 解码器未打开时不得接受区间设置。
    slh::AudioSource closed;
    slh::DecodeThread closedThread(&closed, &ring);
    CHECK_FALSE(closedThread.SetLoopRange(0, 100));
}

// 设置了区间但未开启循环：仍按原语义播到文件末尾并置结束标志。
TEST_CASE(testLoopRangeWithoutLoopingPlaysToEnd)
{
    slh::AudioSource dec;
    CHECK_TRUE(dec.Open(FixturePath("intro_loop_44100_mono.wav").c_str()));
    const int64_t total = dec.Format().totalFrames;

    slh::RingBuffer ring(44100 * 2);
    Drainer drainer(&ring);

    slh::DecodeThread dt(&dec, &ring, 2048);
    CHECK_TRUE(dt.SetLoopRange(1000, 50000));
    CHECK_FALSE(dt.IsLooping());
    CHECK_TRUE(dt.Start());

    CHECK_TRUE(WaitUntil([&] { return dt.ReachedEnd(); }, 8000));
    dt.Stop();
    CHECK_EQ(dt.DecodedFrames(), total);
}

} // namespace

int main()
{
    std::printf("== DecodeThreadTests ==\n");
    testDecodesIntoRingBuffer_runner();
    testDecodesAllFramesAndReachesEnd_runner();
    testWaitsWhenRingBufferFull_runner();
    testStopHaltsDecoding_runner();
    testStartFailsWhenDecoderClosed_runner();
    testStartFailsOnNullPointers_runner();
    testStereoWritesCorrectByteCount_runner();
    testLoopJumpsBackToStart_runner();
    testShortLoopRangeRepeatsBody_runner();
    testStereoLoopKeepsFrameAlignment_runner();
    testLoopRangeValidation_runner();
    testLoopRangeWithoutLoopingPlaysToEnd_runner();
    return Summarize("DecodeThreadTests");
}
