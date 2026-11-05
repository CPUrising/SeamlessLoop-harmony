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

private:
    std::unique_ptr<uint8_t[]> buffer_;
    size_t capacity_ = 0;

    // 单调递增的写入/读取计数，用无符号回绕自然处理环绕。
    // 真实下标 = counter % capacity_。
    std::atomic<uint64_t> writeCounter_{0};
    std::atomic<uint64_t> readCounter_{0};
};

} // namespace slh

#endif // SEAMLESS_LOOP_RING_BUFFER_H
