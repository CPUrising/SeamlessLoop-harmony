/*
 * DecodeThread：独立解码线程。
 *
 * 职责（对应 design.md 决策 2 / 3）：
 * - 在自己的线程里持续从 AudioSource 读 PCM，写入 RingBuffer。
 * - FIFO 满时等待，绝不阻塞 OHAudio 回调线程。
 * 数据源抽象：
 * - AudioSource 对外暴露与 AudioDecoder 相同的 Format / SeekToFrame / ReadFrames，
 *   内部可能是单文件，也可能是 A/B 双文件串成的虚拟时间轴。
 *   因此本类的循环跳转逻辑对两种模式完全一致，无需感知文件切换。
 * 循环跳转与计数：
 * - 循环开启时，解码位置到达 LoopEnd 就在本线程内 seek 回 LoopStart，
 *   因此 FIFO 里永远是「已拼接好的连续数据」，回调侧感知不到循环。
 * - 每完成一次跳转，循环计数加一并通过回调上报（T08）；
 *   关闭循环时不会发生跳转，因此也不会上报（T09）。
 * - 循环关闭时，到文件末尾停止解码，并通过结束回调通知业务层（T07）；
 *   此后写回调只输出静音，音频流保持不中断。
 *
 * 为什么用轮询等待而不是条件变量：
 * 消费者是 OHAudio 的实时回调线程，回调内不允许加锁。若用条件变量，
 * 消费者每次 Read 后都要加锁 notify，违反实时性要求。因此生产者侧
 * 采用短周期轮询（原 WPF 端 AudioLooper 用 Sleep(50) 补数，这里取 5ms，
 * 兼顾响应速度与 CPU 占用）。
 *
 * 对齐说明（AGENTS.md 硬约束 3）：
 * 循环点用帧序号表示，seek 也按帧进行；一帧 = channels × 2 字节 = BlockAlign，
 * 因此跳转后的字节位置天然按 BlockAlign 取整，无需额外的字节级修正。
 *
 * 本类不依赖 OHAudio，可在开发机上单测。
 */

#ifndef SEAMLESS_LOOP_DECODE_THREAD_H
#define SEAMLESS_LOOP_DECODE_THREAD_H

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "audio_source.h"
#include "loop_counter.h"
#include "ring_buffer.h"

namespace slh {

class DecodeThread {
public:
    // chunkFrames：每次从数据源读取的帧数。默认 4096 帧，
    // 48kHz 下约 85ms，足够摊薄解码调用开销。
    DecodeThread(AudioSource* source, RingBuffer* ring, int64_t chunkFrames = 4096);
    ~DecodeThread();

    DecodeThread(const DecodeThread&) = delete;
    DecodeThread& operator=(const DecodeThread&) = delete;

    // 启动解码线程。重复调用无副作用。数据源未打开时返回 false。
    bool Start();

    // 请求停止并等待线程退出。可重复调用。
    void Stop();

    bool IsRunning() const;

    // 累计已解码写入 FIFO 的帧数（循环重复的部分同样计数）。
    int64_t DecodedFrames() const;

    // 是否已读到文件末尾（非循环模式下即播放结束）。
    bool ReachedEnd() const;

    // 设置循环区间（帧）。要求 0 <= start < end <= 总帧数，否则返回 false 且不生效。
    // 运行中调用时（T05 / T06）：若当前播放位置落在新区间之外，丢弃已缓冲数据并跳转到
    // start；仍在区间内则继续播放，不打断。两种情况都会把循环计数归零（T08）。
    bool SetLoopRange(int64_t startFrame, int64_t endFrame);

    // 开关循环。未成功设置过循环区间时，开启不会生效。
    void SetLooping(bool enabled);

    bool IsLooping() const;

    // 循环事件回调：在解码线程上同步执行，参数为累计循环次数（从 1 开始）。
    // 回调内不得做耗时操作；投递到 UI 线程由桥接层负责（任务 5.3）。
    using LoopCallback = LoopCounter::Callback;
    void SetLoopCallback(LoopCallback callback, void* userData);

    // 累计循环次数（T08）。
    int64_t LoopCount() const;

    // 已补偿的播放位置（帧），单调不减，除非发生 seek 或循环点变更（T10）。
    // Intro 阶段（尚未发生跳转）返回真实位置，不叠加补偿（T11）。
    int64_t PlaybackFrame() const;

    // 未补偿的播放位置：解码位置减去 FIFO 中尚未被消费的帧数。
    // 供诊断与对比使用；对外进度应使用 PlaybackFrame()。
    int64_t EstimatePlaybackFrame() const;

    // FIFO 中尚未被消费的帧数。
    int64_t BufferedFrames() const;

    // 重置循环计数。seek 与循环点变更后必须调用（T08）；
    // 任务 5.2 的 seekToSample 需要显式调用本方法。
    void ResetLoopCount();

    // 播放结束回调（T07）：在解码线程上同步执行，且整个生命周期只触发一次。
    using EndCallback = void (*)(void* userData);
    void SetEndCallback(EndCallback callback, void* userData);

    // 跳转到指定帧并等待生效（T14）。
    // 线程运行中：投递请求给解码线程，等它真正 seek 完成才返回；
    // 线程未运行：直接 seek（此时无并发，安全）。
    // 返回 false 表示数据源未打开或 seek 失败。
    bool SeekToFrame(int64_t frame);

private:
    void Run();

    // 当前是否应执行循环跳转（区间有效且开关打开）。
    bool LoopActive(int64_t* startFrame, int64_t* endFrame) const;

    // 请求解码线程跳转到指定帧。实际 seek 在解码线程内完成，
    // 避免在调用线程与解码线程之间并发操作同一个解码器。
    void RequestSeek(int64_t frame);

    // 标记到达末尾并上报（只触发一次）。
    void NotifyEnd();

    AudioSource* source_ = nullptr;
    RingBuffer* ring_ = nullptr;
    int64_t chunkFrames_ = 4096;

    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> reachedEnd_{false};
    std::atomic<int64_t> decodedFrames_{0};

    // 循环区间与开关。读写都在原子变量上，避免解码线程与调用线程竞争。
    std::atomic<int64_t> loopStart_{0};
    std::atomic<int64_t> loopEnd_{0};
    std::atomic<bool> looping_{false};

    // 循环计数与上报（T08 / T09）。
    LoopCounter loopCounter_;

    // 挂起的跳转请求（帧号），-1 表示无请求。由调用线程写、解码线程消费。
    std::atomic<int64_t> pendingSeek_{-1};

    // 已完成的 seek 次数。调用线程据此确认「请求已被解码线程执行完」，
    // 而不是仅仅被取走（取走发生在 seek 之前）。
    std::atomic<uint64_t> seekApplied_{0};

    // 解码线程发布的最新解码位置。CurrentFrame() 不是原子的，
    // 因此跨线程只读这个快照，避免数据竞争。
    std::atomic<int64_t> decodedPosition_{0};

    // 上次返回的播放位置。decodedPosition_ 与 loopCounter_ 不是同一个原子快照，
    // 循环跳转瞬间两者可能不同步，导致读数短暂回退；这里做单调保护（T10）。
    // seek 与循环点变更时重置，允许那两处回退。
    mutable std::atomic<int64_t> lastPlayback_{0};

    // 播放结束上报（T07）。回调只触发一次，之后 EndNotified() 为 true。
    std::atomic<EndCallback> endCallback_{nullptr};
    std::atomic<void*> endUserData_{nullptr};
    std::atomic<bool> endNotified_{false};

    // 解码暂存区，仅本线程访问，无需加锁。
    std::vector<int16_t> scratch_;
};

} // namespace slh

#endif // SEAMLESS_LOOP_DECODE_THREAD_H
