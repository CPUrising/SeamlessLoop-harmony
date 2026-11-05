/*
 * 播放进度单测：覆盖 T10（循环边界进度连续、单调不减）与 T11（Intro 不误补偿）。
 *
 * 分两层：
 * 1) 纯函数 `ComputePlaybackFrame` 的边界与不变量；
 * 2) 真实解码线程在循环中连续采样 `PlaybackFrame()`，验证不出现回退或前跳。
 */

#include "decode_thread.h"
#include "progress.h"
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

constexpr int64_t kTotal = 121275;
constexpr int64_t kLoopStart = 33075;
constexpr int64_t kLoopEnd = 121275;
constexpr int64_t kLoopLen = kLoopEnd - kLoopStart;

// T11：尚未发生跳转时不补偿，进度等于真实解码位置。
TEST_CASE(testNoCompensationDuringIntro)
{
    // Intro 阶段：从 0 推进到 LoopStart，跳转次数恒为 0。
    for (int64_t frame = 0; frame <= kLoopStart; frame += 4096) {
        CHECK_EQ(slh::ComputeCompensatedDecodeFrame(frame, kLoopStart, kLoopEnd, 0), frame);
    }
    CHECK_EQ(slh::ComputeCompensatedDecodeFrame(kLoopStart, kLoopStart, kLoopEnd, 0), kLoopStart);

    // 负的跳转次数同样不得补偿（防御非法入参）。
    CHECK_EQ(slh::ComputeCompensatedDecodeFrame(1000, kLoopStart, kLoopEnd, -1),
             static_cast<int64_t>(1000));
}

// T10：每次跳转补一个循环长度，跳转瞬间进度不回落。
TEST_CASE(testCompensationAddsOneLoopLengthPerJump)
{
    // 跳转 1 次后，解码位置回到 LoopStart，进度应等于 LoopEnd。
    CHECK_EQ(slh::ComputeCompensatedDecodeFrame(kLoopStart, kLoopStart, kLoopEnd, 1), kLoopEnd);
    // 跳转 2 次后，等于 LoopEnd + 一个循环长度。
    CHECK_EQ(slh::ComputeCompensatedDecodeFrame(kLoopStart, kLoopStart, kLoopEnd, 2),
             kLoopEnd + kLoopLen);
    // 循环体内的任意位置同样按 n 倍补偿。
    CHECK_EQ(slh::ComputeCompensatedDecodeFrame(50000, kLoopStart, kLoopEnd, 3),
             static_cast<int64_t>(50000) + kLoopLen * 3);
}

// 退化或非法区间不得参与补偿（避免除零式的无意义偏移）。
TEST_CASE(testDegenerateLoopAddsNothing)
{
    CHECK_EQ(slh::ComputeCompensatedDecodeFrame(1000, 5000, 5000, 3), static_cast<int64_t>(1000));
    CHECK_EQ(slh::ComputeCompensatedDecodeFrame(1000, 5000, 4000, 3), static_cast<int64_t>(1000));
}

// 缓冲扣减：不得返回负数。
TEST_CASE(testBufferedFramesClampedToZero)
{
    CHECK_EQ(slh::ComputePlaybackFrame(100, 0), static_cast<int64_t>(100));
    CHECK_EQ(slh::ComputePlaybackFrame(100, 100), static_cast<int64_t>(0));
    CHECK_EQ(slh::ComputePlaybackFrame(100, 200), static_cast<int64_t>(0));
}

// T10 的数值判据：跨越边界时进度既不回退，也不前跳超过一个循环长度。
TEST_CASE(testProgressContinuousAcrossLoopBoundary)
{
    // 模拟一次边界穿越：跳转前在 LoopEnd-1，跳转后在 LoopStart。
    const int64_t beforeJump = slh::ComputeCompensatedDecodeFrame(kLoopEnd - 1, kLoopStart, kLoopEnd, 0);
    const int64_t afterJump = slh::ComputeCompensatedDecodeFrame(kLoopStart, kLoopStart, kLoopEnd, 1);

    CHECK_EQ(beforeJump, kLoopEnd - 1);
    CHECK_EQ(afterJump, kLoopEnd);
    // 单调不减。
    CHECK_TRUE(afterJump >= beforeJump);
    // 前跳不超过一个循环长度。
    CHECK_TRUE(afterJump - beforeJump <= kLoopLen);
}

// T11 集成：真实解码线程从 0 播到 LoopStart，进度必须等于解码位置。
TEST_CASE(testProgressMatchesDecodePositionDuringIntro)
{
    slh::AudioDecoder dec;
    CHECK_TRUE(dec.Open(FixturePath("intro_loop_44100_mono.wav").c_str()));
    CHECK_EQ(dec.Format().totalFrames, kTotal);

    // 容量设得足够小，保证 FIFO 会被填满，从而进度必然小于解码位置。
    slh::RingBuffer ring(44100);
    slh::DecodeThread dt(&dec, &ring, 1024);
    CHECK_TRUE(dt.SetLoopRange(kLoopStart, kLoopEnd));
    dt.SetLooping(true);
    CHECK_TRUE(dt.Start());

    // 未发生跳转前，PlaybackFrame 必须严格等于「解码位置 − FIFO 未消费帧数」。
    CHECK_TRUE(WaitUntil([&] { return dt.DecodedFrames() > 0; }, 5000));
    CHECK_EQ(dt.LoopCount(), static_cast<int64_t>(0));
    CHECK_EQ(dt.PlaybackFrame(), dt.EstimatePlaybackFrame());

    dt.Stop();
}

// T10 集成：连续跨越多次循环边界，进度采样必须单调不减且不出现大幅前跳。
TEST_CASE(testProgressMonotonicAcrossRealLoops)
{
    slh::AudioDecoder dec;
    CHECK_TRUE(dec.Open(FixturePath("intro_loop_44100_mono.wav").c_str()));

    slh::RingBuffer ring(44100);
    Drainer drainer(&ring);

    slh::DecodeThread dt(&dec, &ring, 1024);
    CHECK_TRUE(dt.SetLoopRange(kLoopStart, kLoopEnd));
    dt.SetLooping(true);
    CHECK_TRUE(dt.Start());

    int64_t last = 0;
    int64_t maxStep = 0;
    int samples = 0;
    bool regressed = false;
    bool jumpedTooFar = false;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline && dt.LoopCount() < 3) {
        const int64_t current = dt.PlaybackFrame();
        if (current < last) {
            regressed = true;
            break;
        }
        const int64_t step = current - last;
        if (step > maxStep) {
            maxStep = step;
        }
        // 前跳超过一个循环长度即视为跳变（T10 判据）。
        if (step > kLoopLen) {
            jumpedTooFar = true;
            break;
        }
        last = current;
        ++samples;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    dt.Stop();

    CHECK_TRUE(samples > 10);
    CHECK_FALSE(regressed);
    CHECK_FALSE(jumpedTooFar);
    CHECK_TRUE(dt.LoopCount() >= 3);
    // 至少跨越了 3 个循环，进度应显著超过一次循环长度。
    CHECK_TRUE(last > kLoopLen);
}

// 改点与 seek 后计数归零，补偿随之清零（进度允许在此处回退）。
TEST_CASE(testCompensationResetAfterRangeChange)
{
    slh::AudioDecoder dec;
    CHECK_TRUE(dec.Open(FixturePath("intro_loop_44100_mono.wav").c_str()));

    slh::RingBuffer ring(44100);
    Drainer drainer(&ring);

    slh::DecodeThread dt(&dec, &ring, 1024);
    CHECK_TRUE(dt.SetLoopRange(kLoopStart, kLoopEnd));
    dt.SetLooping(true);
    CHECK_TRUE(dt.Start());
    CHECK_TRUE(WaitUntil([&] { return dt.LoopCount() >= 1; }, 10000));

    // 改点后计数归零，等价于补偿清零。
    CHECK_TRUE(dt.SetLoopRange(40000, kLoopEnd));
    CHECK_EQ(dt.LoopCount(), static_cast<int64_t>(0));
    CHECK_EQ(dt.PlaybackFrame(), dt.EstimatePlaybackFrame());

    dt.Stop();
}

} // namespace

int main()
{
    std::printf("== ProgressTests ==\n");
    testNoCompensationDuringIntro_runner();
    testCompensationAddsOneLoopLengthPerJump_runner();
    testDegenerateLoopAddsNothing_runner();
    testBufferedFramesClampedToZero_runner();
    testProgressContinuousAcrossLoopBoundary_runner();
    testProgressMatchesDecodePositionDuringIntro_runner();
    testProgressMonotonicAcrossRealLoops_runner();
    testCompensationResetAfterRangeChange_runner();
    return Summarize("ProgressTests");
}
