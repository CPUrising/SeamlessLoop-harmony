/*
 * RingBuffer：单生产者单消费者（SPSC）字节环形缓冲。
 *
 * 用途：解码线程（唯一生产者）写入已解码的 PCM 字节，OHAudio 写回调（唯一消费者）
 * 读出。回调内绝不允许加锁、分配内存或做文件 IO，因此这里用无锁 SPSC 设计。
 *
 * 容量约定（design.md 决策 2）：按 2 秒音频配置，由上层按
 *   sampleRate * channels * sizeof(int16) * 2
 * 计算后传入。
 *
 * 线程安全前提：
 * - 只有一个线程调用 Write，只有一个线程调用 Read。
 * - 多个生产者或多个消费者会破坏正确性，本类不做保护。
 */

#ifndef SEAMLESS_LOOP_RING_BUFFER_H
#define SEAMLESS_LOOP_RING_BUFFER_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace slh {

class RingBuffer {
public:
    // capacityBytes 会被向上取整到不小于请求值。0 视为非法，构造后容量为 0。
    explicit RingBuffer(size_t capacityBytes);
    ~RingBuffer();

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    // 生产者调用：写入最多 bytes 字节，返回实际写入量。
    // 空间不足时写入部分数据，不阻塞。
    size_t Write(const void* data, size_t bytes);

    // 消费者调用：读出最多 bytes 字节，返回实际读出量。
    // 数据不足时读出已有部分，不阻塞。
    size_t Read(void* out, size_t bytes);

    // 消费者调用：读出 bytes 字节；不足部分用静音（0）填充，返回实际有效字节数。
    // 用于写回调——回调必须填满整个 buffer，欠载时以静音代替。
    size_t ReadOrSilence(void* out, size_t bytes);

    size_t AvailableToRead() const;
    size_t AvailableToWrite() const;
    size_t Capacity() const;

    // 清空缓冲。只在生产者与消费者都停止时调用（例如 seek 前）。
    void Clear();

    // 生产者调用：请求丢弃当前缓冲内容（运行时 seek / 改循环点，T05）。
    // 实际丢弃由消费者在下次 Read 时完成——readCounter_ 始终只由消费者写，
    // 维持 SPSC 前提；因此本请求不会立刻生效，最多延迟一个回调周期。
    void RequestFlush();

    // 消费者已应用的丢弃次数。用于诊断与测试观察「缓冲确实被清空」。
    uint64_t FlushCount() const;

    // 已请求的丢弃次数（含尚未应用的）。生产者据此判断「这块数据是否已过期」：
    // 解码前后代次变化，说明期间发生了 seek / 改点，本块必须丢弃。
    uint64_t FlushRequestCount() const;

    // 是否还有尚未被消费者应用的丢弃请求。生产者据此决定是否暂缓写入新数据，
    // 否则新数据会被随后应用的丢弃一并作废。
    bool HasPendingFlush() const;

private:
    std::unique_ptr<uint8_t[]> buffer_;
    size_t capacity_ = 0;

    // 单调递增的写入/读取计数，用无符号回绕自然处理环绕。
    // 真实下标 = counter % capacity_。
    std::atomic<uint64_t> writeCounter_{0};
    std::atomic<uint64_t> readCounter_{0};

    // 丢弃请求计数（生产者写）与已应用计数（消费者写）。
    std::atomic<uint64_t> flushRequested_{0};
    std::atomic<uint64_t> flushApplied_{0};
};

} // namespace slh

#endif // SEAMLESS_LOOP_RING_BUFFER_H
