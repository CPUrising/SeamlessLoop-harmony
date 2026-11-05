/*
 * DecodeThread 实现。
 */

#include "decode_thread.h"

#include <chrono>

#include "progress.h"

namespace slh {
namespace {

// FIFO 满时的等待周期。取 5ms：比 WPF 端的 50ms 更细，减少欠载概率，
// 同时避免忙等把 CPU 打满。
constexpr int kFullWaitMs = 5;

} // namespace

DecodeThread::DecodeThread(AudioDecoder* decoder, RingBuffer* ring, int64_t chunkFrames)
    : decoder_(decoder), ring_(ring), chunkFrames_(chunkFrames > 0 ? chunkFrames : 4096)
{
}

DecodeThread::~DecodeThread()
{
    Stop();
}

bool DecodeThread::Start()
{
    if (running_.load(std::memory_order_acquire)) {
        return true;
    }
    if (decoder_ == nullptr || ring_ == nullptr || !decoder_->IsOpen()) {
        return false;
    }

    const int32_t channels = decoder_->Format().channels;
    if (channels <= 0) {
        return false;
    }

    // 一帧 = channels 个 int16。按帧读取，天然满足 BlockAlign 对齐。
    scratch_.assign(static_cast<size_t>(chunkFrames_ * channels), 0);

    stop_.store(false, std::memory_order_release);
    reachedEnd_.store(false, std::memory_order_release);
    endNotified_.store(false, std::memory_order_release);
    decodedFrames_.store(0, std::memory_order_release);
    decodedPosition_.store(decoder_->CurrentFrame(), std::memory_order_release);
    running_.store(true, std::memory_order_release);

    thread_ = std::thread(&DecodeThread::Run, this);
    return true;
}

void DecodeThread::Stop()
{
    stop_.store(true, std::memory_order_release);
    if (thread_.joinable()) {
        thread_.join();
    }
    running_.store(false, std::memory_order_release);
}

bool DecodeThread::IsRunning() const
{
    return running_.load(std::memory_order_acquire);
}

int64_t DecodeThread::DecodedFrames() const
{
    return decodedFrames_.load(std::memory_order_acquire);
}

bool DecodeThread::ReachedEnd() const
{
    return reachedEnd_.load(std::memory_order_acquire);
}

bool DecodeThread::SetLoopRange(int64_t startFrame, int64_t endFrame)
{
    if (decoder_ == nullptr || !decoder_->IsOpen()) {
        return false;
    }
    const int64_t total = decoder_->Format().totalFrames;
    // 内部不变量防护：越界或退化区间一律不生效，避免解码线程忙转。
    // 业务层的归一化与错误上报由 LoopConfig 负责（任务 4.1）。
    if (startFrame < 0 || endFrame <= startFrame || endFrame > total) {
        return false;
    }

    // 先判断位置再改区间：判断依据是「新」区间，所以用参数而非成员。
    const int64_t playback = EstimatePlaybackFrame();
    const bool wasLooping = looping_.load(std::memory_order_acquire);
    const bool outsideNewRange = playback < startFrame || playback >= endFrame;

    loopStart_.store(startFrame, std::memory_order_release);
    loopEnd_.store(endFrame, std::memory_order_release);
    // 循环点变更后计数归零（T08）。
    loopCounter_.Reset();

    // T05：正在循环播放且当前位置落在新区间外——丢弃已缓冲数据并跳回新区间起点。
    // 只在循环开启时跳转；循环关闭时区间不影响播放，不应打断。
    if (wasLooping && outsideNewRange) {
        // 先清空 FIFO，避免把旧区间残留的音频播出去。
        ring_->RequestFlush();
        RequestSeek(startFrame);
    }
    // T06：位置仍在新区间内，什么都不做，从当前位置继续播放。
    return true;
}

void DecodeThread::SetLooping(bool enabled)
{
    if (enabled) {
        // 没有有效区间时不开循环，否则解码线程会在 LoopEnd 处原地打转。
        const int64_t start = loopStart_.load(std::memory_order_acquire);
        const int64_t end = loopEnd_.load(std::memory_order_acquire);
        if (start < 0 || end <= start) {
            return;
        }
    }
    looping_.store(enabled, std::memory_order_release);
}

bool DecodeThread::IsLooping() const
{
    return looping_.load(std::memory_order_acquire);
}

void DecodeThread::SetLoopCallback(LoopCallback callback, void* userData)
{
    loopCounter_.SetCallback(callback, userData);
}

int64_t DecodeThread::LoopCount() const
{
    return loopCounter_.Count();
}

void DecodeThread::ResetLoopCount()
{
    loopCounter_.Reset();
}

int64_t DecodeThread::BufferedFrames() const
{
    if (decoder_ == nullptr || !decoder_->IsOpen()) {
        return 0;
    }
    const int32_t channels = decoder_->Format().channels;
    if (channels <= 0) {
        return 0;
    }
    const size_t frameBytes = static_cast<size_t>(channels) * sizeof(int16_t);
    return static_cast<int64_t>(ring_->AvailableToRead() / frameBytes);
}

int64_t DecodeThread::EstimatePlaybackFrame() const
{
    if (decoder_ == nullptr || !decoder_->IsOpen()) {
        return 0;
    }
    // 解码位置减 FIFO 未消费部分，近似扬声器位置。
    const int64_t position =
        decodedPosition_.load(std::memory_order_acquire) - BufferedFrames();
    return position > 0 ? position : 0;
}

int64_t DecodeThread::PlaybackFrame() const
{
    if (decoder_ == nullptr || !decoder_->IsOpen()) {
        return 0;
    }
    // 用原始解码位置做补偿，而不是已扣减缓冲的估算值：
    // 补偿量只与跳转次数有关，扣减缓冲必须放在补偿之后（见 progress.h）。
    return ComputePlaybackFrame(decodedPosition_.load(std::memory_order_acquire),
                                BufferedFrames(),
                                loopStart_.load(std::memory_order_acquire),
                                loopEnd_.load(std::memory_order_acquire),
                                loopCounter_.Count());
}

void DecodeThread::RequestSeek(int64_t frame)
{
    pendingSeek_.store(frame, std::memory_order_release);
}

void DecodeThread::SetEndCallback(EndCallback callback, void* userData)
{
    // 先存 userData 再存回调，避免回调先可见、数据后可见的窗口。
    endUserData_.store(userData, std::memory_order_release);
    endCallback_.store(callback, std::memory_order_release);
}

void DecodeThread::NotifyEnd()
{
    reachedEnd_.store(true, std::memory_order_release);
    // 只上报一次：重复到达末尾（例如 seek 回前面又播完）不再触发。
    if (endNotified_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    const EndCallback callback = endCallback_.load(std::memory_order_acquire);
    if (callback != nullptr) {
        callback(endUserData_.load(std::memory_order_acquire));
    }
}

bool DecodeThread::LoopActive(int64_t* startFrame, int64_t* endFrame) const
{
    if (!looping_.load(std::memory_order_acquire)) {
        return false;
    }
    const int64_t start = loopStart_.load(std::memory_order_acquire);
    const int64_t end = loopEnd_.load(std::memory_order_acquire);
    if (start < 0 || end <= start) {
        return false;
    }
    *startFrame = start;
    *endFrame = end;
    return true;
}

void DecodeThread::Run()
{
    const int32_t channels = decoder_->Format().channels;
    const size_t frameBytes = static_cast<size_t>(channels) * sizeof(int16_t);

    while (!stop_.load(std::memory_order_acquire)) {
        // 处理挂起的跳转请求（运行时改点 T05 / 后续 seek 接口）。
        // seek 必须在解码线程内做：解码器状态不是线程安全的。
        const int64_t seekTarget = pendingSeek_.exchange(-1, std::memory_order_acq_rel);
        if (seekTarget >= 0) {
            if (decoder_->SeekToFrame(seekTarget) < 0) {
                reachedEnd_.store(true, std::memory_order_release);
                break;
            }
            decodedPosition_.store(decoder_->CurrentFrame(), std::memory_order_release);
            reachedEnd_.store(false, std::memory_order_release);
            continue;
        }

        // 有未应用的丢弃请求时不要写入，否则新数据会被随后应用的丢弃一并作废。
        if (ring_->HasPendingFlush()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kFullWaitMs));
            continue;
        }

        // FIFO 剩余空间不足一帧就等，避免读到半帧数据。
        if (ring_->AvailableToWrite() < frameBytes) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kFullWaitMs));
            continue;
        }

        // 按 FIFO 可写空间限制本次读取量，避免读到放不下的数据。
        const int64_t writableFrames = static_cast<int64_t>(ring_->AvailableToWrite() / frameBytes);
        int64_t frames = writableFrames < chunkFrames_ ? writableFrames : chunkFrames_;
        if (frames <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kFullWaitMs));
            continue;
        }

        // 循环跳转（T01）：到达 LoopEnd 就在本线程 seek 回 LoopStart。
        // 不重开文件、不重建渲染器，音频流完全无感。
        int64_t loopStart = 0;
        int64_t loopEnd = 0;
        if (LoopActive(&loopStart, &loopEnd)) {
            const int64_t pos = decoder_->CurrentFrame();
            if (pos >= loopEnd) {
                // 按帧 seek 天然满足 BlockAlign 对齐（一帧 = channels × 2 字节）。
                if (decoder_->SeekToFrame(loopStart) < 0) {
                    // 区间已校验过，理论上不可达；失败按播放结束处理，避免忙转。
                    reachedEnd_.store(true, std::memory_order_release);
                    break;
                }
                // 跳转成功才算一次循环（T08）；循环关闭时不会走到这里（T09）。
                loopCounter_.IncrementAndNotify();
                decodedPosition_.store(decoder_->CurrentFrame(), std::memory_order_release);
                continue; // 重新按新位置计算本次可读帧数
            }
            // 不越过 LoopEnd 多读，否则 FIFO 里会出现本该被跳过的数据。
            const int64_t remaining = loopEnd - pos;
            if (frames > remaining) {
                frames = remaining;
            }
        }

        // 记录解码前的丢弃代次：解码耗时期间若发生改点/seek，这块数据就作废。
        const uint64_t flushGenerationAtRead = ring_->FlushRequestCount();

        const int64_t got = decoder_->ReadFrames(scratch_.data(), frames);
        if (got <= 0) {
            // 到达文件末尾（循环关闭时的正常结束）：停止推进并上报（T07）。
            // 线程退出后写回调继续输出静音，音频流不中断。
            NotifyEnd();
            break;
        }
        // 发布解码位置快照，供跨线程估算播放位置（T05 / T06 与任务 5.1）。
        decodedPosition_.store(decoder_->CurrentFrame(), std::memory_order_release);

        // 这块数据可能读自「改点前」的位置：解码期间若发生过丢弃或跳转请求，
        // 它就不再属于新的播放位置，必须整块丢弃，否则会残留旧音频（T05）。
        if (ring_->FlushRequestCount() != flushGenerationAtRead ||
            pendingSeek_.load(std::memory_order_acquire) >= 0) {
            continue;
        }

        // 写入 FIFO。Write 不阻塞，返回实际写入字节数。
        const size_t bytes = static_cast<size_t>(got) * frameBytes;
        size_t written = 0;
        while (written < bytes && !stop_.load(std::memory_order_acquire)) {
            const size_t n = ring_->Write(
                reinterpret_cast<const uint8_t*>(scratch_.data()) + written, bytes - written);
            if (n == 0) {
                // FIFO 又满了，等一下再写剩下的。
                std::this_thread::sleep_for(std::chrono::milliseconds(kFullWaitMs));
                continue;
            }
            written += n;
        }
        decodedFrames_.fetch_add(got, std::memory_order_acq_rel);
    }

    running_.store(false, std::memory_order_release);
}

} // namespace slh
