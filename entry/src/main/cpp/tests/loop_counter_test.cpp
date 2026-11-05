/*
 * LoopCounter 单测：覆盖 T08（循环次数单调递增、可重置）与 T09（关闭循环不上报）。
 *
 * 两类用例：
 * 1) 纯计数类语义（重置、回调累计值、无回调安全）；
 * 2) 与 DecodeThread 的集成（真实解码器 + 合成 fixture 驱动真实循环跳转）。
 */

#include "decode_thread.h"
#include "loop_counter.h"
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

// 持续排空 FIFO，模拟 OHAudio 回调线程；否则解码线程会因 FIFO 满而停住。
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

private:
    void Loop()
    {
        std::vector<uint8_t> buf(4096);
        while (!stop_.load()) {
            if (ring_->Read(buf.data(), buf.size()) == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }

    slh::RingBuffer* ring_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

// 回调事件记录：解码线程写，测试线程读，全部走原子变量。
struct LoopEvents {
    std::atomic<int> count{0};
    std::atomic<int64_t> last{0};
    std::atomic<int> sequenceBroken{0};
};

void OnLoop(void* userData, int64_t loopCount)
{
    auto* events = static_cast<LoopEvents*>(userData);
    const int64_t expected = events->last.load() + 1;
    if (loopCount != expected) {
        events->sequenceBroken.store(1);
    }
    events->last.store(loopCount);
    events->count.fetch_add(1);
}

// 初始为 0，重置后仍为 0。
TEST_CASE(testStartsAtZeroAndResets)
{
    slh::LoopCounter counter;
    CHECK_EQ(counter.Count(), static_cast<int64_t>(0));
    counter.Reset();
    CHECK_EQ(counter.Count(), static_cast<int64_t>(0));
}

// 每次递增都上报累计值，返回值与 Count() 一致。
TEST_CASE(testIncrementNotifiesCumulativeCount)
{
    slh::LoopCounter counter;
    LoopEvents events;
    counter.SetCallback(OnLoop, &events);

    CHECK_EQ(counter.IncrementAndNotify(), static_cast<int64_t>(1));
    CHECK_EQ(counter.IncrementAndNotify(), static_cast<int64_t>(2));
    CHECK_EQ(counter.IncrementAndNotify(), static_cast<int64_t>(3));
    CHECK_EQ(counter.Count(), static_cast<int64_t>(3));
    CHECK_EQ(events.count.load(), 3);
    CHECK_EQ(events.last.load(), static_cast<int64_t>(3));
    CHECK_EQ(events.sequenceBroken.load(), 0);
}

// 重置后从 1 重新计数，而不是接着之前的数值。
TEST_CASE(testResetRestartsFromOne)
{
    slh::LoopCounter counter;
    LoopEvents events;
    counter.SetCallback(OnLoop, &events);

    counter.IncrementAndNotify();
    counter.IncrementAndNotify();
    CHECK_EQ(counter.Count(), static_cast<int64_t>(2));

    counter.Reset();
    CHECK_EQ(counter.Count(), static_cast<int64_t>(0));
    CHECK_EQ(counter.IncrementAndNotify(), static_cast<int64_t>(1));
    CHECK_EQ(events.count.load(), 3);
    CHECK_EQ(events.last.load(), static_cast<int64_t>(1));
}

// 未登记回调时递增不得崩溃。
TEST_CASE(testIncrementWithoutCallbackIsSafe)
{
    slh::LoopCounter counter;
    CHECK_EQ(counter.IncrementAndNotify(), static_cast<int64_t>(1));
    CHECK_EQ(counter.Count(), static_cast<int64_t>(1));
}

// T08：真实循环跳转时计数单调递增并回调；循环点变更与显式重置后归零。
TEST_CASE(testCounterIncrementsAndResets)
{
    slh::AudioDecoder dec;
    CHECK_TRUE(dec.Open(FixturePath("intro_loop_44100_mono.wav").c_str()));
    const int64_t total = dec.Format().totalFrames;

    slh::RingBuffer ring(44100 * 2);
    Drainer drainer(&ring);

    slh::DecodeThread dt(&dec, &ring, 2048);
    LoopEvents events;
    dt.SetLoopCallback(OnLoop, &events);

    CHECK_TRUE(dt.SetLoopRange(33075, total));
    dt.SetLooping(true);
    CHECK_EQ(dt.LoopCount(), static_cast<int64_t>(0));
    CHECK_TRUE(dt.Start());

    CHECK_TRUE(WaitUntil([&] { return dt.LoopCount() >= 2; }, 10000));
    dt.Stop();

    const int64_t reached = dt.LoopCount();
    CHECK_TRUE(reached >= 2);
    // 回调收到的累计值必须与计数器一致，且单调递增（1,2,3...）。
    CHECK_EQ(events.last.load(), reached);
    CHECK_EQ(events.count.load(), static_cast<int>(reached));
    CHECK_EQ(events.sequenceBroken.load(), 0);

    // 循环点变更后计数归零（T08）。
    CHECK_TRUE(dt.SetLoopRange(40000, total));
    CHECK_EQ(dt.LoopCount(), static_cast<int64_t>(0));

    // 重新计数仍从 1 开始。
    CHECK_TRUE(dt.Start());
    CHECK_TRUE(WaitUntil([&] { return dt.LoopCount() >= 1; }, 10000));
    dt.Stop();
    CHECK_TRUE(dt.LoopCount() >= 1);

    // 显式重置（任务 5.2 的 seek 会调用它）。
    dt.ResetLoopCount();
    CHECK_EQ(dt.LoopCount(), static_cast<int64_t>(0));
}

// 播放中途开启循环：计数从开启后的第一次跳转开始。
TEST_CASE(testCounterOnlyCountsAfterLoopingEnabled)
{
    slh::AudioDecoder dec;
    CHECK_TRUE(dec.Open(FixturePath("intro_loop_44100_mono.wav").c_str()));
    const int64_t total = dec.Format().totalFrames;

    slh::RingBuffer ring(44100 * 2);
    Drainer drainer(&ring);

    slh::DecodeThread dt(&dec, &ring, 2048);
    CHECK_TRUE(dt.SetLoopRange(33075, total));
    CHECK_FALSE(dt.IsLooping());

    CHECK_TRUE(dt.Start());
    // 循环未开启时不会发生跳转，计数保持 0。
    CHECK_TRUE(WaitUntil([&] { return dt.DecodedFrames() > 0; }, 3000));
    CHECK_EQ(dt.LoopCount(), static_cast<int64_t>(0));

    dt.SetLooping(true);
    CHECK_TRUE(WaitUntil([&] { return dt.LoopCount() >= 1; }, 10000));
    dt.Stop();
    CHECK_TRUE(dt.LoopCount() >= 1);
}

// T09：关闭循环播到末尾，不得触发任何循环事件。
TEST_CASE(testNoEventWhenDisabled)
{
    slh::AudioDecoder dec;
    CHECK_TRUE(dec.Open(FixturePath("intro_loop_44100_mono.wav").c_str()));
    const int64_t total = dec.Format().totalFrames;

    slh::RingBuffer ring(44100 * 2);
    Drainer drainer(&ring);

    slh::DecodeThread dt(&dec, &ring, 2048);
    LoopEvents events;
    dt.SetLoopCallback(OnLoop, &events);
    // 设置区间但不开启循环。
    CHECK_TRUE(dt.SetLoopRange(1000, 50000));

    CHECK_TRUE(dt.Start());
    CHECK_TRUE(WaitUntil([&] { return dt.ReachedEnd(); }, 8000));
    dt.Stop();

    CHECK_EQ(dt.LoopCount(), static_cast<int64_t>(0));
    CHECK_EQ(events.count.load(), 0);
    CHECK_EQ(dt.DecodedFrames(), total);
}

} // namespace

int main()
{
    std::printf("== LoopCounterTests ==\n");
    testStartsAtZeroAndResets_runner();
    testIncrementNotifiesCumulativeCount_runner();
    testResetRestartsFromOne_runner();
    testIncrementWithoutCallbackIsSafe_runner();
    testCounterIncrementsAndResets_runner();
    testCounterOnlyCountsAfterLoopingEnabled_runner();
    testNoEventWhenDisabled_runner();
    return Summarize("LoopCounterTests");
}
