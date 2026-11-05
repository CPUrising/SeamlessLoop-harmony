/*
 * 播放控制单测：覆盖 T13（暂停恢复不丢位置）与 T14（按采样点跳转）。
 *
 * 用真实解码器与真实解码线程驱动，断言端到端可观测行为。
 */

#include "playback_engine.h"
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

// 消费者：模拟 OHAudio 回调持续取数，否则 FIFO 会满、解码线程停住。
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

// 加载后状态为 Ready，格式信息可用，播放从 0 开始（T12 的补充断言）。
TEST_CASE(testLoadReportsFormatAndStartsAtZero)
{
    slh::PlaybackEngine engine;
    CHECK_TRUE(engine.Load(FixturePath("intro_loop_44100_mono.wav").c_str()));
    CHECK_TRUE(engine.State() == slh::PlaybackState::kReady);
    CHECK_EQ(engine.TotalFrames(), kTotal);
    CHECK_EQ(engine.Format().sampleRate, 44100);
    CHECK_EQ(engine.Format().channels, 1);
    CHECK_EQ(engine.DecodeFrame(), static_cast<int64_t>(0));
    CHECK_EQ(engine.PlaybackFrame(), static_cast<int64_t>(0));
    CHECK_FALSE(engine.IsPlaying());
}

// 未加载时所有控制接口必须安全失败，不得崩溃。
TEST_CASE(testControlsFailWhenNotLoaded)
{
    slh::PlaybackEngine engine;
    CHECK_FALSE(engine.Play());
    CHECK_FALSE(engine.SeekToSample(100));
    CHECK_EQ(engine.TotalFrames(), static_cast<int64_t>(0));
    CHECK_EQ(engine.PlaybackFrame(), static_cast<int64_t>(0));
    CHECK_EQ(engine.LoopCount(), static_cast<int64_t>(0));
    CHECK_FALSE(engine.IsLooping());
    // 未加载时调用这些也不得崩溃。
    engine.Pause();
    engine.Stop();
    engine.SetLooping(true);
    CHECK_FALSE(engine.SetLoopRange(0, 100));
}

// 加载失败时状态回到 Idle。
TEST_CASE(testLoadFailureKeepsIdle)
{
    slh::PlaybackEngine engine;
    CHECK_FALSE(engine.Load("no_such_file.wav"));
    CHECK_TRUE(engine.State() == slh::PlaybackState::kIdle);
    CHECK_FALSE(engine.Load(nullptr));
    CHECK_TRUE(engine.State() == slh::PlaybackState::kIdle);
}

// T13：暂停后恢复，位置不得丢失（偏差 < 100ms）。
TEST_CASE(testPauseResumeKeepsPosition)
{
    slh::PlaybackEngine engine;
    CHECK_TRUE(engine.Load(FixturePath("intro_loop_44100_mono.wav").c_str()));
    Drainer drainer(engine.Ring());

    CHECK_TRUE(engine.Play());
    CHECK_TRUE(engine.IsPlaying());
    CHECK_TRUE(WaitUntil([&] { return engine.DecodeFrame() > 20000; }, 5000));

    engine.Pause();
    CHECK_TRUE(engine.State() == slh::PlaybackState::kPaused);
    CHECK_FALSE(engine.IsPlaying());

    const int64_t pausedDecode = engine.DecodeFrame();
    const int64_t pausedPlayback = engine.PlaybackFrame();
    CHECK_TRUE(pausedDecode > 0);

    // 暂停期间位置不得推进。
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK_EQ(engine.DecodeFrame(), pausedDecode);

    CHECK_TRUE(engine.Play());
    CHECK_TRUE(engine.IsPlaying());

    // 恢复后位置必须从暂停点附近继续，而不是回到 0。
    CHECK_TRUE(engine.DecodeFrame() >= pausedDecode);
    // 100ms @44.1kHz = 4410 帧；恢复点与暂停点的偏差必须小于该值。
    CHECK_TRUE(engine.DecodeFrame() - pausedDecode < 4410);
    CHECK_TRUE(engine.PlaybackFrame() >= pausedPlayback);

    engine.Stop();
}

// T14：seek 到指定采样点，位置精确落点且 FIFO 被清空。
TEST_CASE(testSeekClampsAndAligns)
{
    slh::PlaybackEngine engine;
    CHECK_TRUE(engine.Load(FixturePath("intro_loop_44100_mono.wav").c_str()));

    // 未播放时 seek：直接生效。
    CHECK_TRUE(engine.SeekToSample(50000));
    CHECK_EQ(engine.DecodeFrame(), static_cast<int64_t>(50000));
    CHECK_EQ(engine.PlaybackFrame(), static_cast<int64_t>(50000));
    CHECK_EQ(engine.Ring()->AvailableToRead(), static_cast<size_t>(0));

    // 越界 clamp 到 [0, total]。
    CHECK_TRUE(engine.SeekToSample(-1));
    CHECK_EQ(engine.DecodeFrame(), static_cast<int64_t>(0));
    CHECK_TRUE(engine.SeekToSample(kTotal + 999999));
    CHECK_EQ(engine.DecodeFrame(), kTotal);
    CHECK_TRUE(engine.SeekToSample(kTotal / 2));
    CHECK_EQ(engine.DecodeFrame(), kTotal / 2);
}

// T14：播放中 seek 必须清空缓冲，不得播出跳转前的残留音频。
TEST_CASE(testSeekWhilePlayingFlushesBuffer)
{
    slh::PlaybackEngine engine;
    CHECK_TRUE(engine.Load(FixturePath("intro_loop_44100_mono.wav").c_str()));
    Drainer drainer(engine.Ring());

    CHECK_TRUE(engine.Play());
    CHECK_TRUE(WaitUntil([&] { return engine.DecodeFrame() > 10000; }, 5000));

    const uint64_t flushBefore = engine.Ring()->FlushCount();
    CHECK_TRUE(engine.SeekToSample(90000));
    // 播放中解码线程会立刻从新位置继续预填 FIFO（容量 2 秒 = 88200 帧），
    // 因此不能断言精确等于 90000，只能断言落点落在 [90000, 90000 + 2 秒] 内。
    const int64_t landed = engine.DecodeFrame();
    CHECK_TRUE(landed >= 90000);
    CHECK_TRUE(landed <= 90000 + 88200);
    // 丢弃请求必须已被消费者应用。
    CHECK_TRUE(engine.Ring()->FlushCount() > flushBefore);
    CHECK_FALSE(engine.Ring()->HasPendingFlush());

    engine.Stop();
}

// seek 后循环计数（即补偿）必须归零。
TEST_CASE(testSeekResetsLoopCount)
{
    slh::PlaybackEngine engine;
    CHECK_TRUE(engine.Load(FixturePath("intro_loop_44100_mono.wav").c_str()));
    Drainer drainer(engine.Ring());

    CHECK_TRUE(engine.SetLoopRange(33075, kTotal));
    engine.SetLooping(true);
    CHECK_TRUE(engine.Play());
    CHECK_TRUE(WaitUntil([&] { return engine.LoopCount() >= 1; }, 10000));

    CHECK_TRUE(engine.SeekToSample(1000));
    CHECK_EQ(engine.LoopCount(), static_cast<int64_t>(0));
    // 补偿清零后，进度等于未补偿估算值。
    CHECK_EQ(engine.PlaybackFrame(), engine.Decoder()->EstimatePlaybackFrame());

    engine.Stop();
}

// Stop 语义：回到文件开头、清空缓冲、计数归零、状态回到 Ready。
TEST_CASE(testStopResetsToStart)
{
    slh::PlaybackEngine engine;
    CHECK_TRUE(engine.Load(FixturePath("intro_loop_44100_mono.wav").c_str()));
    Drainer drainer(engine.Ring());

    CHECK_TRUE(engine.Play());
    CHECK_TRUE(WaitUntil([&] { return engine.DecodeFrame() > 10000; }, 5000));

    engine.Stop();
    CHECK_TRUE(engine.State() == slh::PlaybackState::kReady);
    CHECK_FALSE(engine.IsPlaying());
    CHECK_EQ(engine.DecodeFrame(), static_cast<int64_t>(0));
    CHECK_EQ(engine.LoopCount(), static_cast<int64_t>(0));
    CHECK_EQ(engine.Ring()->AvailableToRead(), static_cast<size_t>(0));

    // 停止后可再次播放。
    CHECK_TRUE(engine.Play());
    CHECK_TRUE(engine.IsPlaying());
    engine.Stop();
}

// 重复 Play / Pause 必须幂等，不得启动多个线程或崩溃。
TEST_CASE(testPlayPauseAreIdempotent)
{
    slh::PlaybackEngine engine;
    CHECK_TRUE(engine.Load(FixturePath("intro_loop_44100_mono.wav").c_str()));
    Drainer drainer(engine.Ring());

    CHECK_TRUE(engine.Play());
    CHECK_TRUE(engine.Play());
    CHECK_TRUE(engine.IsPlaying());

    engine.Pause();
    engine.Pause();
    CHECK_TRUE(engine.State() == slh::PlaybackState::kPaused);

    CHECK_TRUE(engine.Play());
    CHECK_TRUE(engine.IsPlaying());
    engine.Stop();
    engine.Stop();
    CHECK_TRUE(engine.State() == slh::PlaybackState::kReady);
}

// 重新加载另一个文件：旧状态必须完全释放。
TEST_CASE(testReloadReleasesPreviousFile)
{
    slh::PlaybackEngine engine;
    CHECK_TRUE(engine.Load(FixturePath("intro_loop_44100_mono.wav").c_str()));
    CHECK_EQ(engine.TotalFrames(), kTotal);

    CHECK_TRUE(engine.Load(FixturePath("intro_loop_48000_stereo.wav").c_str()));
    CHECK_EQ(engine.TotalFrames(), static_cast<int64_t>(132000));
    CHECK_EQ(engine.Format().sampleRate, 48000);
    CHECK_EQ(engine.Format().channels, 2);
    CHECK_EQ(engine.DecodeFrame(), static_cast<int64_t>(0));
    CHECK_TRUE(engine.State() == slh::PlaybackState::kReady);
}

} // namespace

int main()
{
    std::printf("== TransportTests ==\n");
    testLoadReportsFormatAndStartsAtZero_runner();
    testControlsFailWhenNotLoaded_runner();
    testLoadFailureKeepsIdle_runner();
    testPauseResumeKeepsPosition_runner();
    testSeekClampsAndAligns_runner();
    testSeekWhilePlayingFlushesBuffer_runner();
    testSeekResetsLoopCount_runner();
    testStopResetsToStart_runner();
    testPlayPauseAreIdempotent_runner();
    testReloadReleasesPreviousFile_runner();
    return Summarize("TransportTests");
}
