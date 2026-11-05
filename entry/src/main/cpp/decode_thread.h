/*
 * DecodeThread：独立解码线程。
 *
 * 职责（对应 design.md 决策 2）：
 * - 在自己的线程里持续从 AudioDecoder 读 PCM，写入 RingBuffer。
 * - FIFO 满时等待，绝不阻塞 OHAudio 回调线程。
 * - 到文件末尾时停止解码（循环跳转由任务 4.2 在此基础上加入）。
 *
 * 为什么用轮询等待而不是条件变量：
 * 消费者是 OHAudio 的实时回调线程，回调内不允许加锁。若用条件变量，
 * 消费者每次 Read 后都要加锁 notify，违反实时性要求。因此生产者侧
 * 采用短周期轮询（原 WPF 端 AudioLooper 用 Sleep(50) 补数，这里取 5ms，
 * 兼顾响应速度与 CPU 占用）。
 *
 * 本类不依赖 OHAudio，可在开发机上单测。
 */

#ifndef SEAMLESS_LOOP_DECODE_THREAD_H
#define SEAMLESS_LOOP_DECODE_THREAD_H

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "audio_decoder.h"
#include "ring_buffer.h"

namespace slh {

class DecodeThread {
public:
    // chunkFrames：每次从解码器读取的帧数。默认 4096 帧，
    // 48kHz 下约 85ms，足够摊薄解码调用开销。
    DecodeThread(AudioDecoder* decoder, RingBuffer* ring, int64_t chunkFrames = 4096);
    ~DecodeThread();

    DecodeThread(const DecodeThread&) = delete;
    DecodeThread& operator=(const DecodeThread&) = delete;

    // 启动解码线程。重复调用无副作用。decoder 未打开时返回 false。
    bool Start();

    // 请求停止并等待线程退出。可重复调用。
    void Stop();

    bool IsRunning() const;

    // 累计已解码写入 FIFO 的帧数。
    int64_t DecodedFrames() const;

    // 是否已读到文件末尾（非循环模式下即播放结束）。
    bool ReachedEnd() const;

private:
    void Run();

    AudioDecoder* decoder_ = nullptr;
    RingBuffer* ring_ = nullptr;
    int64_t chunkFrames_ = 4096;

    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> reachedEnd_{false};
    std::atomic<int64_t> decodedFrames_{0};

    // 解码暂存区，仅本线程访问，无需加锁。
    std::vector<int16_t> scratch_;
};

} // namespace slh

#endif // SEAMLESS_LOOP_DECODE_THREAD_H
