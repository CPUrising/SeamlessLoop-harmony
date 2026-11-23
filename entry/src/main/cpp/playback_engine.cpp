/*
 * PlaybackEngine 实现。
 */

#include "playback_engine.h"

#include <chrono>
#include <thread>

#include "ab_pair.h"

namespace slh {
namespace {

// FIFO 容量按 2 秒音频配置（design.md 决策 2）。
constexpr int64_t kBufferSeconds = 2;

// 等待消费者应用丢弃请求的上限。OHAudio 回调周期通常 10~20ms。
constexpr int kFlushWaitMs = 200;

size_t ComputeRingCapacity(const AudioFormatInfo& info)
{
    const int64_t bytesPerSecond =
        static_cast<int64_t>(info.sampleRate) * info.bytesPerFrame();
    if (bytesPerSecond <= 0) {
        return 0;
    }
    return static_cast<size_t>(bytesPerSecond * kBufferSeconds);
}

} // namespace

PlaybackEngine::PlaybackEngine() = default;

PlaybackEngine::~PlaybackEngine()
{
    ReleaseAll();
}

bool PlaybackEngine::Load(const char* path)
{
    ReleaseAll();

    if (path == nullptr) {
        return false;
    }

    // 先尝试 A/B 配对：命中则按虚拟时间轴加载，否则退化为单文件（P02 / P03）。
    auto source = std::make_unique<AudioSource>();
    const AbPairResult pair = DetectAbPair(path);
    if (pair.isPair) {
        if (!source->OpenPair(pair.aPath.c_str(), pair.bPath.c_str())) {
            return false;
        }
    } else if (!source->Open(path)) {
        return false;
    }

    const size_t capacity = ComputeRingCapacity(source->Format());
    if (capacity == 0) {
        return false;
    }

    source_ = std::move(source);
    ring_ = std::make_unique<RingBuffer>(capacity);
    decodeThread_ = std::make_unique<DecodeThread>(source_.get(), ring_.get());
    state_ = PlaybackState::kReady;
    return true;
}

void PlaybackEngine::Unload()
{
    ReleaseAll();
}

void PlaybackEngine::ReleaseAll()
{
    if (decodeThread_ != nullptr) {
        decodeThread_->Stop();
    }
    decodeThread_.reset();
    ring_.reset();
    source_.reset();
    state_ = PlaybackState::kIdle;
}

bool PlaybackEngine::Play()
{
    if (state_ == PlaybackState::kIdle || decodeThread_ == nullptr) {
        return false;
    }
    if (state_ == PlaybackState::kPlaying) {
        return true;
    }
    // 暂停后恢复：解码线程从解码器当前位置继续，FIFO 内容保留（T13）。
    if (!decodeThread_->Start()) {
        return false;
    }
    state_ = PlaybackState::kPlaying;
    return true;
}

void PlaybackEngine::Pause()
{
    if (state_ != PlaybackState::kPlaying || decodeThread_ == nullptr) {
        return;
    }
    // 只停线程：不清 FIFO、不动解码位置，恢复时位置连续（T13）。
    decodeThread_->Stop();
    state_ = PlaybackState::kPaused;
}

void PlaybackEngine::Stop()
{
    if (state_ == PlaybackState::kIdle || decodeThread_ == nullptr) {
        return;
    }
    decodeThread_->Stop();
    // 停止语义：回到文件开头，清空残留数据，补偿归零。
    decodeThread_->SeekToFrame(0);
    FlushBuffer();
    decodeThread_->ResetLoopCount();
    state_ = PlaybackState::kReady;
}

void PlaybackEngine::FlushBuffer()
{
    if (ring_ == nullptr) {
        return;
    }
    ring_->RequestFlush();
    // 消费者在跑时会自己应用丢弃请求；等它应用完再返回。
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kFlushWaitMs);
    while (ring_->HasPendingFlush() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (ring_->HasPendingFlush()) {
        // 超时说明没有消费者在跑（例如渲染器已停），此时直接清空是安全的。
        ring_->Clear();
    }
}

bool PlaybackEngine::SeekToSample(int64_t frame)
{
    if (state_ == PlaybackState::kIdle || decodeThread_ == nullptr) {
        return false;
    }

    // 先清空 FIFO，避免播出跳转前的残留音频（T14）。
    FlushBuffer();

    if (!decodeThread_->SeekToFrame(frame)) {
        return false;
    }
    // seek 后补偿清零（T08 / T14）。
    decodeThread_->ResetLoopCount();
    return true;
}

int64_t PlaybackEngine::PlaybackFrame() const
{
    if (decodeThread_ == nullptr) {
        return 0;
    }
    return decodeThread_->PlaybackFrame();
}

int64_t PlaybackEngine::DecodeFrame() const
{
    if (source_ == nullptr) {
        return 0;
    }
    return source_->CurrentFrame();
}

int64_t PlaybackEngine::TotalFrames() const
{
    if (source_ == nullptr) {
        return 0;
    }
    return source_->Format().totalFrames;
}

const AudioFormatInfo& PlaybackEngine::Format() const
{
    static const AudioFormatInfo kEmpty{};
    if (source_ == nullptr) {
        return kEmpty;
    }
    return source_->Format();
}

bool PlaybackEngine::SetLoopRange(int64_t startFrame, int64_t endFrame)
{
    if (decodeThread_ == nullptr) {
        return false;
    }
    return decodeThread_->SetLoopRange(startFrame, endFrame);
}

void PlaybackEngine::SetLooping(bool enabled)
{
    if (decodeThread_ != nullptr) {
        decodeThread_->SetLooping(enabled);
    }
}

bool PlaybackEngine::IsLooping() const
{
    return decodeThread_ != nullptr && decodeThread_->IsLooping();
}

int64_t PlaybackEngine::LoopCount() const
{
    return decodeThread_ != nullptr ? decodeThread_->LoopCount() : 0;
}

void PlaybackEngine::SetLoopCallback(DecodeThread::LoopCallback callback, void* userData)
{
    if (decodeThread_ != nullptr) {
        decodeThread_->SetLoopCallback(callback, userData);
    }
}

void PlaybackEngine::SetEndCallback(DecodeThread::EndCallback callback, void* userData)
{
    if (decodeThread_ != nullptr) {
        decodeThread_->SetEndCallback(callback, userData);
    }
}

} // namespace slh
