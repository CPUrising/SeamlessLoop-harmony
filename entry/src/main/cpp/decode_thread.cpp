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
        const int64_t frames = writableFrames < chunkFrames_ ? writableFrames : chunkFrames_;
        if (frames <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kFullWaitMs));
            continue;
        }

        const int64_t got = decoder_->ReadFrames(scratch_.data(), frames);
        if (got <= 0) {
            // 到达文件末尾。循环跳转在任务 4.2 接入此处。
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
