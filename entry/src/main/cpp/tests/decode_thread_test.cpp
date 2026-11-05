/*
 * DecodeThread 单测：验证「持续解码 → 写入 FIFO → 到末尾停止」。
 *
 * 用真实合成 fixture（44.1kHz 单声道 WAV，121275 帧）驱动真实解码器，
 * 因此这条链路是端到端可判定的，不依赖 OHAudio 与真机。
 */

#include "decode_thread.h"
#include "test_common.h"

#include <chrono>
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

// 启动后必须真的把数据解码进 FIFO。
TEST_CASE(testDecodesIntoRingBuffer)
{
    slh::AudioDecoder dec;
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
    slh::AudioDecoder dec;
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
    slh::AudioDecoder dec;
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
    slh::AudioDecoder dec;
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
    slh::AudioDecoder dec;
    slh::RingBuffer ring(1024);
    slh::DecodeThread dt(&dec, &ring);
    CHECK_FALSE(dt.Start());
    CHECK_FALSE(dt.IsRunning());
    dt.Stop(); // 必须可安全重复调用
}

// 空指针防护。
TEST_CASE(testStartFailsOnNullPointers)
{
    slh::AudioDecoder dec;
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
    slh::AudioDecoder dec;
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
    return Summarize("DecodeThreadTests");
}
