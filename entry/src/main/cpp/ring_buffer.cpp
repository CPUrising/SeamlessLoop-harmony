/*
 * RingBuffer 实现：无锁 SPSC。
 *
 * 内存序说明：
 * - 生产者写完数据后，用 release 语义发布 writeCounter_；
 * - 消费者读数据前，用 acquire 语义读取 writeCounter_。
 * 这样保证消费者看到 writeCounter_ 时，对应的数据一定已写入。
 * 反向同理：消费者用 release 发布 readCounter_，生产者用 acquire 读取。
 */

#include "ring_buffer.h"

#include <cstring>

namespace slh {

RingBuffer::RingBuffer(size_t capacityBytes)
{
    if (capacityBytes == 0) {
        return;
    }
    capacity_ = capacityBytes;
    buffer_.reset(new uint8_t[capacity_]);
    std::memset(buffer_.get(), 0, capacity_);
}

RingBuffer::~RingBuffer() = default;

size_t RingBuffer::Write(const void* data, size_t bytes)
{
    if (capacity_ == 0 || data == nullptr || bytes == 0) {
        return 0;
    }

    const uint64_t writePos = writeCounter_.load(std::memory_order_relaxed);
    // 必须 acquire：确保读到消费者已释放的空间。
    const uint64_t readPos = readCounter_.load(std::memory_order_acquire);

    const size_t used = static_cast<size_t>(writePos - readPos);
    const size_t freeBytes = capacity_ - used;
    const size_t toWrite = bytes < freeBytes ? bytes : freeBytes;
    if (toWrite == 0) {
        return 0;
    }

    const size_t startIndex = static_cast<size_t>(writePos % capacity_);
    const size_t firstChunk = (capacity_ - startIndex) < toWrite ? (capacity_ - startIndex) : toWrite;
    std::memcpy(buffer_.get() + startIndex, data, firstChunk);
    if (toWrite > firstChunk) {
        std::memcpy(buffer_.get(), static_cast<const uint8_t*>(data) + firstChunk, toWrite - firstChunk);
    }

    // release：数据写入必须早于计数发布对消费者可见。
    writeCounter_.store(writePos + toWrite, std::memory_order_release);
    return toWrite;
}

size_t RingBuffer::Read(void* out, size_t bytes)
{
    if (capacity_ == 0 || out == nullptr || bytes == 0) {
        return 0;
    }

    // 先应用挂起的丢弃请求（T05）：把读指针推到写指针，旧数据全部作废。
    // 只有消费者会写 readCounter_ 与 flushApplied_，因此仍满足 SPSC。
    const uint64_t flushRequested = flushRequested_.load(std::memory_order_acquire);
    if (flushRequested != flushApplied_.load(std::memory_order_relaxed)) {
        flushApplied_.store(flushRequested, std::memory_order_release);
        const uint64_t writePos = writeCounter_.load(std::memory_order_acquire);
        readCounter_.store(writePos, std::memory_order_release);
        return 0; // 本次回调交出静音，让解码线程重新填充
    }

    const uint64_t readPos = readCounter_.load(std::memory_order_relaxed);
    // 必须 acquire：确保读到生产者已发布的数据。
    const uint64_t writePos = writeCounter_.load(std::memory_order_acquire);

    const size_t available = static_cast<size_t>(writePos - readPos);
    const size_t toRead = bytes < available ? bytes : available;
    if (toRead == 0) {
        return 0;
    }

    const size_t startIndex = static_cast<size_t>(readPos % capacity_);
    const size_t firstChunk = (capacity_ - startIndex) < toRead ? (capacity_ - startIndex) : toRead;
    std::memcpy(out, buffer_.get() + startIndex, firstChunk);
    if (toRead > firstChunk) {
        std::memcpy(static_cast<uint8_t*>(out) + firstChunk, buffer_.get(), toRead - firstChunk);
    }

    // release：读完才能把空间还给生产者。
    readCounter_.store(readPos + toRead, std::memory_order_release);
    return toRead;
}

size_t RingBuffer::ReadOrSilence(void* out, size_t bytes)
{
    if (out == nullptr || bytes == 0) {
        return 0;
    }
    const size_t read = Read(out, bytes);
    if (read < bytes) {
        std::memset(static_cast<uint8_t*>(out) + read, 0, bytes - read);
    }
    return read;
}

size_t RingBuffer::AvailableToRead() const
{
    if (capacity_ == 0) {
        return 0;
    }
    const uint64_t writePos = writeCounter_.load(std::memory_order_acquire);
    const uint64_t readPos = readCounter_.load(std::memory_order_acquire);
    return static_cast<size_t>(writePos - readPos);
}

size_t RingBuffer::AvailableToWrite() const
{
    if (capacity_ == 0) {
        return 0;
    }
    return capacity_ - AvailableToRead();
}

size_t RingBuffer::Capacity() const
{
    return capacity_;
}

void RingBuffer::Clear()
{
    // 先读后写，顺序不重要——前提是调用时生产者与消费者都已停止。
    const uint64_t writePos = writeCounter_.load(std::memory_order_acquire);
    readCounter_.store(writePos, std::memory_order_release);
}

void RingBuffer::RequestFlush()
{
    flushRequested_.fetch_add(1, std::memory_order_acq_rel);
}

uint64_t RingBuffer::FlushCount() const
{
    return flushApplied_.load(std::memory_order_acquire);
}

bool RingBuffer::HasPendingFlush() const
{
    return flushRequested_.load(std::memory_order_acquire) !=
           flushApplied_.load(std::memory_order_acquire);
}

uint64_t RingBuffer::FlushRequestCount() const
{
    return flushRequested_.load(std::memory_order_acquire);
}

} // namespace slh
