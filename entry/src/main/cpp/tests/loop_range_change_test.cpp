/*
 * 运行时修改循环点单测：覆盖 T05（位置在新区间外则清空 FIFO 并跳转）
 * 与 T06（位置仍在新区间内则继续播放，不打断）。
 *
 * 用真实合成 fixture 与真实解码线程驱动，因此断言的是端到端可观测行为：
 * - 跳转后新写入 FIFO 的第一个字节必须等于 fixture 中 LoopStart 处的采样；
 * - 不跳转时解码位置必须继续单调推进。
 */

#include "decode_thread.h"
#include "test_common.h"

#include <chrono>
#include <cstdint>
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

// 把整首 fixture 解成 S16 交错 PCM，作为「正确跳转目标」的参考。
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

// 消费者：持续排空 FIFO，并记录每次读到的第一帧，用于观察跳转落点。
class ProbeConsumer {
public:
    explicit ProbeConsumer(slh::RingBuffer* ring) : ring_(ring), thread_([this] { Loop(); }) {}

    ~ProbeConsumer() { Stop(); }

    void Stop()
    {
        stop_.store(true);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    size_t TotalRead() const { return totalRead_.load(); }

    // 开始观察「下一次丢弃生效之后」的数据。
    // 不能只清标志：改点请求发出后，消费者可能仍读到改点前的残留数据并抢先记录，
    // 造成偶发失败。这里记下期望的丢弃代次，只有代次达标后才开始记录。
    void ResetObservation()
    {
        totalRead_.store(0);
        observed_.store(false);
        expectedFlush_.store(ring_->FlushCount() + 1);
    }

    int16_t FirstSampleAfterReset() const { return firstSample_.load(); }
    bool HasObservation() const { return observed_.load(); }

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
            // 丢弃尚未生效时读到的都是旧数据，跳过。
            if (ring_->FlushCount() < expectedFlush_.load()) {
                continue;
            }
            if (!observed_.load()) {
                firstSample_.store(*reinterpret_cast<const int16_t*>(buf.data()));
                observed_.store(true);
            }
            totalRead_.fetch_add(n);
        }
    }

    slh::RingBuffer* ring_;
    std::atomic<bool> stop_{false};
    std::atomic<size_t> totalRead_{0};
    std::atomic<bool> observed_{false};
    std::atomic<int16_t> firstSample_{0};
    std::atomic<uint64_t> expectedFlush_{0};
    std::thread thread_;
};

// 循环事件记录：用于判定「改点后计数确实从 1 重新开始」。
// 不能直接断言 LoopCount()==0——改点后解码线程会立刻继续计数，读取时已非 0。
struct LoopEventRecorder {
    std::atomic<bool> sawRestartFromOne{false};

    static void OnLoop(void* userData, int64_t loopCount)
    {
        auto* self = static_cast<LoopEventRecorder*>(userData);
        if (loopCount == 1) {
            self->sawRestartFromOne.store(true);
        }
    }
};

// T05：播放中把区间改到当前位置之外——必须跳转到新区间起点，且不残留旧数据。
TEST_CASE(testChangeOutsideRangeSeeks)
{
    const std::string path = FixturePath("intro_loop_44100_mono.wav");
    int64_t total = 0;
    const std::vector<int16_t> all = ReadWholeFile(path, &total);
    CHECK_EQ(total, static_cast<int64_t>(121275));

    slh::AudioSource dec;
    CHECK_TRUE(dec.Open(path.c_str()));
    slh::RingBuffer ring(44100 * 2);
    ProbeConsumer consumer(&ring);

    slh::DecodeThread dt(&dec, &ring, 1024);
    LoopEventRecorder recorder;
    dt.SetLoopCallback(LoopEventRecorder::OnLoop, &recorder);
    // 原区间：靠前的 [33075, 40000)。
    CHECK_TRUE(dt.SetLoopRange(33075, 40000));
    dt.SetLooping(true);
    CHECK_TRUE(dt.Start());

    // 先让它真的开始播放原区间。
    CHECK_TRUE(WaitUntil([&] { return consumer.TotalRead() > 0; }, 5000));
    // 等解码位置推进到原区间内，确保确实处在「正在播放」的状态。
    CHECK_TRUE(WaitUntil([&] { return dt.LoopCount() >= 1; }, 10000));

    // 新区间起点远在原位置之后：[80000, total)。
    const int64_t newStart = 80000;
    consumer.ResetObservation();
    recorder.sawRestartFromOne.store(false);
    CHECK_TRUE(dt.SetLoopRange(newStart, total));

    // 跳转后读到的第一个采样必须来自新区间起点（fixture 是相位连续的合成波，
    // 直接比对采样值即可判定落点）。
    CHECK_TRUE(WaitUntil([&] { return consumer.HasObservation(); }, 5000));
    CHECK_EQ(consumer.FirstSampleAfterReset(), all[static_cast<size_t>(newStart)]);

    // 改点后计数归零（T08）：回调必须重新报出 1，而不是接着之前的数值递增。
    CHECK_TRUE(WaitUntil([&] { return recorder.sawRestartFromOne.load(); }, 10000));

    dt.Stop();
    CHECK_TRUE(dt.IsRunning() == false);
}

// T05 的另一半：清空 FIFO，不能把旧区间的残留音频播出去。
TEST_CASE(testChangeOutsideRangeFlushesBufferedAudio)
{
    slh::AudioSource dec;
    CHECK_TRUE(dec.Open(FixturePath("intro_loop_44100_mono.wav").c_str()));
    const int64_t total = dec.Format().totalFrames;

    slh::RingBuffer ring(44100 * 2);
    // 丢弃请求由消费者在 Read 时应用，因此必须有消费者（真实场景是 OHAudio 回调）。
    ProbeConsumer consumer(&ring);

    slh::DecodeThread dt(&dec, &ring, 2048);
    CHECK_TRUE(dt.SetLoopRange(1000, 50000));
    dt.SetLooping(true);
    CHECK_TRUE(dt.Start());

    CHECK_TRUE(WaitUntil([&] { return consumer.TotalRead() > 0; }, 5000));
    const uint64_t before = ring.FlushCount();

    // 新区间完全在当前位置之后，触发清空 + 跳转。
    CHECK_TRUE(dt.SetLoopRange(90000, total));
    CHECK_TRUE(WaitUntil([&] { return ring.FlushCount() > before; }, 5000));

    dt.Stop();
    // 丢弃请求必须已被消费者应用，否则残留数据会继续被播出。
    CHECK_TRUE(ring.FlushCount() > before);
    CHECK_FALSE(ring.HasPendingFlush());
}

// T06：播放中把区间改到仍包含当前位置——不得跳转，继续播放。
TEST_CASE(testChangeInsideRangeKeepsPosition)
{
    slh::AudioSource dec;
    CHECK_TRUE(dec.Open(FixturePath("intro_loop_44100_mono.wav").c_str()));
    const int64_t total = dec.Format().totalFrames;

    slh::RingBuffer ring(44100 * 2);
    ProbeConsumer consumer(&ring);

    slh::DecodeThread dt(&dec, &ring, 1024);
    // 先播放 [0, 30000)，当前位置落在其中。
    CHECK_TRUE(dt.SetLoopRange(0, 30000));
    dt.SetLooping(true);
    CHECK_TRUE(dt.Start());
    CHECK_TRUE(WaitUntil([&] { return consumer.TotalRead() > 0; }, 5000));

    // 新区间 [0, total) 仍包含当前位置，不应跳转。
    const uint64_t flushBefore = ring.FlushCount();
    const int64_t framesBeforeChange = dt.DecodedFrames();
    CHECK_TRUE(dt.SetLoopRange(0, total));

    // 给解码线程充分时间；若发生跳转，FlushCount 会增加。
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK_EQ(ring.FlushCount(), flushBefore);
    CHECK_FALSE(ring.HasPendingFlush());

    // 继续推进：解码帧数应持续增长，而不是停在原地（证明没有被打断）。
    CHECK_TRUE(dt.DecodedFrames() > framesBeforeChange);
    const int64_t framesAfterChange = dt.DecodedFrames();
    CHECK_TRUE(WaitUntil([&] { return dt.DecodedFrames() > framesAfterChange; }, 5000));

    dt.Stop();
}

// 循环未开启时改区间：只更新区间与计数，不得跳转（不应打断普通播放）。
TEST_CASE(testChangeWhileNotLoopingDoesNotSeek)
{
    slh::AudioSource dec;
    CHECK_TRUE(dec.Open(FixturePath("intro_loop_44100_mono.wav").c_str()));
    const int64_t total = dec.Format().totalFrames;

    slh::RingBuffer ring(44100 * 2);
    ProbeConsumer consumer(&ring);

    slh::DecodeThread dt(&dec, &ring, 1024);
    CHECK_TRUE(dt.Start());
    CHECK_TRUE(WaitUntil([&] { return consumer.TotalRead() > 0; }, 5000));

    const uint64_t flushBefore = ring.FlushCount();
    CHECK_TRUE(dt.SetLoopRange(90000, total));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    CHECK_EQ(ring.FlushCount(), flushBefore);
    CHECK_FALSE(ring.HasPendingFlush());
    dt.Stop();
}

// 改点后新区间仍能正常循环：跳转落点与循环体都必须正确。
TEST_CASE(testLoopStillWorksAfterRangeChange)
{
    const std::string path = FixturePath("intro_loop_44100_mono.wav");
    int64_t total = 0;
    const std::vector<int16_t> all = ReadWholeFile(path, &total);

    slh::AudioSource dec;
    CHECK_TRUE(dec.Open(path.c_str()));
    slh::RingBuffer ring(44100 * 2);
    ProbeConsumer consumer(&ring);

    slh::DecodeThread dt(&dec, &ring, 1024);
    CHECK_TRUE(dt.SetLoopRange(0, 20000));
    dt.SetLooping(true);
    CHECK_TRUE(dt.Start());
    CHECK_TRUE(WaitUntil([&] { return dt.LoopCount() >= 1; }, 10000));

    const int64_t newStart = 60000;
    consumer.ResetObservation();
    CHECK_TRUE(dt.SetLoopRange(newStart, total));
    CHECK_TRUE(WaitUntil([&] { return consumer.HasObservation(); }, 5000));
    CHECK_EQ(consumer.FirstSampleAfterReset(), all[static_cast<size_t>(newStart)]);

    // 新区间长度为 61275 帧，足以再触发一次跳转。
    CHECK_TRUE(WaitUntil([&] { return dt.LoopCount() >= 1; }, 15000));
    dt.Stop();
    CHECK_TRUE(dt.LoopCount() >= 1);
}

} // namespace

int main()
{
    std::printf("== LoopRangeChangeTests ==\n");
    testChangeOutsideRangeSeeks_runner();
    testChangeOutsideRangeFlushesBufferedAudio_runner();
    testChangeInsideRangeKeepsPosition_runner();
    testChangeWhileNotLoopingDoesNotSeek_runner();
    testLoopStillWorksAfterRangeChange_runner();
    return Summarize("LoopRangeChangeTests");
}
