/*
 * LoopStream 单测：覆盖 T07（关闭循环后播到末尾停止并上报结束事件）。
 *
 * 断言的是端到端可观测行为：
 * - 关闭循环后不再发生循环跳转，循环计数冻结；
 * - 播到文件末尾时解码线程停止推进，结束回调恰好触发一次；
 * - 结束后写回调仍能持续输出静音（音频流不被中断）。
 */

#include "decode_thread.h"
#include "test_common.h"

#include <chrono>
#include <cstdint>
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

template <typename Pred>
bool WaitUntil(Pred pred, int timeoutMs = 10000)
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

// 持续排空 FIFO，模拟 OHAudio 回调线程。
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

struct EndEvents {
    std::atomic<int> count{0};

    static void OnEnd(void* userData)
    {
        static_cast<EndEvents*>(userData)->count.fetch_add(1);
    }
};

// T07 主场景：循环播放中关闭循环，播到末尾停止并上报。
TEST_CASE(testLoopDisabledStopsAtEnd)
{
    slh::AudioDecoder dec;
    CHECK_TRUE(dec.Open(FixturePath("intro_loop_44100_mono.wav").c_str()));
    const int64_t total = dec.Format().totalFrames;

    slh::RingBuffer ring(44100 * 2);
    Drainer drainer(&ring);

    slh::DecodeThread dt(&dec, &ring, 2048);
    EndEvents ends;
    dt.SetEndCallback(EndEvents::OnEnd, &ends);

    CHECK_TRUE(dt.SetLoopRange(33075, 20000 + 33075));
    dt.SetLooping(true);
    CHECK_TRUE(dt.Start());

    // 先确认真的在循环。
    CHECK_TRUE(WaitUntil([&] { return dt.LoopCount() >= 2; }, 10000));
    CHECK_EQ(ends.count.load(), 0);
    CHECK_FALSE(dt.ReachedEnd());

    // 关闭循环：后续不再跳转，直接播到文件末尾。
    dt.SetLooping(false);
    CHECK_FALSE(dt.IsLooping());

    CHECK_TRUE(WaitUntil([&] { return dt.ReachedEnd(); }, 10000));
    CHECK_EQ(ends.count.load(), 1);

    // 循环计数必须冻结（T09 同一语义：关闭后不再上报循环事件）。
    const int64_t frozen = dt.LoopCount();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK_EQ(dt.LoopCount(), frozen);

    dt.Stop();
    CHECK_FALSE(dt.IsRunning());
    // 结束回调仍只触发一次。
    CHECK_EQ(ends.count.load(), 1);
    // 解码线程在末尾停止后不得再推进位置到 total 之后。
    CHECK_TRUE(dec.CurrentFrame() <= total);
}

// 结束后写回调仍应输出静音（T07 的「保持静音输出」）。
TEST_CASE(testSilenceAfterEnd)
{
    slh::AudioDecoder dec;
    CHECK_TRUE(dec.Open(FixturePath("intro_loop_44100_mono.wav").c_str()));

    slh::RingBuffer ring(44100 * 2);
    Drainer drainer(&ring);

    slh::DecodeThread dt(&dec, &ring, 2048);
    EndEvents ends;
    dt.SetEndCallback(EndEvents::OnEnd, &ends);

    // 不开启循环，直接整曲播完。
    CHECK_TRUE(dt.Start());
    CHECK_TRUE(WaitUntil([&] { return dt.ReachedEnd(); }, 10000));
    dt.Stop();
    CHECK_EQ(ends.count.load(), 1);

    // 等消费者把 FIFO 排空。
    CHECK_TRUE(WaitUntil([&] { return ring.AvailableToRead() == 0; }, 5000));
    drainer.Stop();

    // 排空后回调仍必须交出静音，而不是残留数据或崩溃。
    uint8_t out[1024];
    std::memset(out, 0xFF, sizeof(out));
    const size_t valid = ring.ReadOrSilence(out, sizeof(out));
    CHECK_EQ(valid, static_cast<size_t>(0));
    bool allZero = true;
    for (size_t i = 0; i < sizeof(out); ++i) {
        if (out[i] != 0) {
            allZero = false;
            break;
        }
    }
    CHECK_TRUE(allZero);
}

// 结束事件只触发一次：多等一会儿也不会重复上报。
TEST_CASE(testEndEventFiresExactlyOnce)
{
    slh::AudioDecoder dec;
    CHECK_TRUE(dec.Open(FixturePath("intro_loop_44100_mono.wav").c_str()));

    slh::RingBuffer ring(44100 * 2);
    Drainer drainer(&ring);

    slh::DecodeThread dt(&dec, &ring, 2048);
    EndEvents ends;
    dt.SetEndCallback(EndEvents::OnEnd, &ends);
    CHECK_TRUE(dt.Start());

    CHECK_TRUE(WaitUntil([&] { return dt.ReachedEnd(); }, 10000));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK_EQ(ends.count.load(), 1);
    dt.Stop();
    CHECK_EQ(ends.count.load(), 1);
}

// 循环期间永远不该触发结束事件（循环跳转使解码位置不会推进到 EOF）。
TEST_CASE(testNoEndEventWhileLooping)
{
    slh::AudioDecoder dec;
    CHECK_TRUE(dec.Open(FixturePath("intro_loop_44100_mono.wav").c_str()));

    slh::RingBuffer ring(44100 * 2);
    Drainer drainer(&ring);

    slh::DecodeThread dt(&dec, &ring, 2048);
    EndEvents ends;
    dt.SetEndCallback(EndEvents::OnEnd, &ends);
    CHECK_TRUE(dt.SetLoopRange(33075, 121275));
    dt.SetLooping(true);
    CHECK_TRUE(dt.Start());

    CHECK_TRUE(WaitUntil([&] { return dt.LoopCount() >= 3; }, 15000));
    CHECK_EQ(ends.count.load(), 0);
    CHECK_FALSE(dt.ReachedEnd());

    dt.Stop();
    CHECK_EQ(ends.count.load(), 0);
}

// 未登记结束回调时到达末尾不得崩溃。
TEST_CASE(testEndWithoutCallbackIsSafe)
{
    slh::AudioDecoder dec;
    CHECK_TRUE(dec.Open(FixturePath("intro_loop_44100_mono.wav").c_str()));

    slh::RingBuffer ring(44100 * 2);
    Drainer drainer(&ring);

    slh::DecodeThread dt(&dec, &ring, 2048);
    CHECK_TRUE(dt.Start());
    CHECK_TRUE(WaitUntil([&] { return dt.ReachedEnd(); }, 10000));
    dt.Stop();
    CHECK_TRUE(dt.ReachedEnd());
}

} // namespace

int main()
{
    std::printf("== LoopStreamTests ==\n");
    testLoopDisabledStopsAtEnd_runner();
    testSilenceAfterEnd_runner();
    testEndEventFiresExactlyOnce_runner();
    testNoEndEventWhileLooping_runner();
    testEndWithoutCallbackIsSafe_runner();
    return Summarize("LoopStreamTests");
}
