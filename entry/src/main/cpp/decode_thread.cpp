/*
 * DecodeThread 实现。
 */

#include "decode_thread.h"

#include <chrono>

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
    decodedFrames_.store(0, std::memory_order_release);
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
    loopStart_.store(startFrame, std::memory_order_release);
    loopEnd_.store(endFrame, std::memory_order_release);
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
                continue; // 重新按新位置计算本次可读帧数
            }
            // 不越过 LoopEnd 多读，否则 FIFO 里会出现本该被跳过的数据。
            const int64_t remaining = loopEnd - pos;
            if (frames > remaining) {
                frames = remaining;
            }
        }

        const int64_t got = decoder_->ReadFrames(scratch_.data(), frames);
        if (got <= 0) {
            // 到达文件末尾。循环开启时不应走到这里（LoopEnd 处已跳转）。
            reachedEnd_.store(true, std::memory_order_release);
            break;
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
