/*
 * RingBuffer 单测：覆盖 T17 的欠载静音填充，以及环绕、边界、容量行为。
 */

#include "ring_buffer.h"
#include "test_common.h"

#include <cstdint>
#include <vector>

namespace {

// 构造一段递增字节，便于校验顺序与环绕。
std::vector<uint8_t> MakePattern(size_t count, uint8_t seed)
{
    std::vector<uint8_t> v(count);
    for (size_t i = 0; i < count; ++i) {
        v[i] = static_cast<uint8_t>(seed + i);
    }
    return v;
}

// T17：数据不足时用静音填充，而不是崩溃或返回未初始化内存。
TEST_CASE(testUnderrunWritesSilence)
{
    slh::RingBuffer rb(64);
    const std::vector<uint8_t> in = MakePattern(10, 0x11);
    CHECK_EQ(rb.Write(in.data(), in.size()), static_cast<size_t>(10));
    CHECK_EQ(rb.AvailableToRead(), static_cast<size_t>(10));

    uint8_t out[32];
    // 预填 0xFF，以便区分「静音填充」与「未初始化」。
    std::memset(out, 0xFF, sizeof(out));
    const size_t valid = rb.ReadOrSilence(out, sizeof(out));

    CHECK_EQ(valid, static_cast<size_t>(10));
    for (size_t i = 0; i < 10; ++i) {
        CHECK_EQ(out[i], in[i]);
    }
    // 欠载部分必须是 0，而不是 0xFF。
    for (size_t i = 10; i < sizeof(out); ++i) {
        CHECK_EQ(out[i], static_cast<uint8_t>(0));
    }
    CHECK_EQ(rb.AvailableToRead(), static_cast<size_t>(0));
}

// 空缓冲时 ReadOrSilence 必须写满静音。
TEST_CASE(testReadOrSilenceOnEmptyBuffer)
{
    slh::RingBuffer rb(32);
    uint8_t out[16];
    std::memset(out, 0xAA, sizeof(out));
    CHECK_EQ(rb.ReadOrSilence(out, sizeof(out)), static_cast<size_t>(0));
    for (size_t i = 0; i < sizeof(out); ++i) {
        CHECK_EQ(out[i], static_cast<uint8_t>(0));
    }
}

// 写入超出容量时只写能写下的部分，不越界、不丢已有数据。
TEST_CASE(testWriteClampsToCapacity)
{
    slh::RingBuffer rb(16);
    const std::vector<uint8_t> in = MakePattern(40, 1);
    CHECK_EQ(rb.Write(in.data(), in.size()), static_cast<size_t>(16));
    CHECK_EQ(rb.AvailableToRead(), static_cast<size_t>(16));
    CHECK_EQ(rb.AvailableToWrite(), static_cast<size_t>(0));

    // 满了之后再写必须返回 0。
    CHECK_EQ(rb.Write(in.data(), in.size()), static_cast<size_t>(0));

    uint8_t out[16];
    CHECK_EQ(rb.Read(out, sizeof(out)), static_cast<size_t>(16));
    for (size_t i = 0; i < 16; ++i) {
        CHECK_EQ(out[i], in[i]);
    }
}

// 跨环绕点读写：验证 firstChunk / 第二段拷贝逻辑。
TEST_CASE(testWrapAround)
{
    slh::RingBuffer rb(16);
    const std::vector<uint8_t> a = MakePattern(12, 0);
    CHECK_EQ(rb.Write(a.data(), a.size()), static_cast<size_t>(12));

    uint8_t tmp[8];
    CHECK_EQ(rb.Read(tmp, 8), static_cast<size_t>(8)); // 读走后 writePos 到 12，readPos 到 8

    // 此时剩余 4 字节（下标 8..11），再写 10 字节会跨过 16 回绕到 0..5。
    const std::vector<uint8_t> b = MakePattern(10, 100);
    CHECK_EQ(rb.Write(b.data(), b.size()), static_cast<size_t>(10));

    uint8_t out[14];
    CHECK_EQ(rb.Read(out, sizeof(out)), static_cast<size_t>(14));

    // 前 4 字节是 a 的尾部（a[8..11]），后 10 字节是 b。
    for (size_t i = 0; i < 4; ++i) {
        CHECK_EQ(out[i], a[8 + i]);
    }
    for (size_t i = 0; i < 10; ++i) {
        CHECK_EQ(out[4 + i], b[i]);
    }
}

// Clear 后容量全部可用。
TEST_CASE(testClearReleasesAll)
{
    slh::RingBuffer rb(32);
    const std::vector<uint8_t> in = MakePattern(20, 5);
    CHECK_EQ(rb.Write(in.data(), in.size()), static_cast<size_t>(20));
    rb.Clear();
    CHECK_EQ(rb.AvailableToRead(), static_cast<size_t>(0));
    CHECK_EQ(rb.AvailableToWrite(), static_cast<size_t>(32));
}

// 零容量缓冲必须安全退化，不崩溃。
TEST_CASE(testZeroCapacityIsSafe)
{
    slh::RingBuffer rb(0);
    uint8_t byte = 1;
    CHECK_EQ(rb.Capacity(), static_cast<size_t>(0));
    CHECK_EQ(rb.Write(&byte, 1), static_cast<size_t>(0));
    CHECK_EQ(rb.Read(&byte, 1), static_cast<size_t>(0));
    CHECK_EQ(rb.ReadOrSilence(&byte, 1), static_cast<size_t>(0));
    CHECK_EQ(rb.AvailableToRead(), static_cast<size_t>(0));
    CHECK_EQ(rb.AvailableToWrite(), static_cast<size_t>(0));
}

// 2 秒容量换算：48kHz / 双声道 / S16 = 192000 字节/秒。
TEST_CASE(testTwoSecondCapacityMath)
{
    const size_t bytesPerSecond = 48000u * 2u * sizeof(int16_t);
    slh::RingBuffer rb(bytesPerSecond * 2);
    CHECK_EQ(rb.Capacity(), static_cast<size_t>(384000));
    CHECK_EQ(rb.AvailableToWrite(), static_cast<size_t>(384000));
}

// T17 第 2 条：缓冲恢复后自动继续播放。
//
// 场景：先让消费者把 FIFO 读空（触发欠载），生产者再写入新数据，
// 消费者必须能继续读到新数据，且内容正确、无残留错位。
TEST_CASE(testRecoversAfterUnderrun)
{
    slh::RingBuffer rb(32);

    // 阶段 1：写入 16 字节后读空，模拟欠载发生。
    const std::vector<uint8_t> first = MakePattern(16, 10);
    CHECK_EQ(rb.Write(first.data(), first.size()), static_cast<size_t>(16));

    uint8_t out[32];
    std::memset(out, 0xFF, sizeof(out));
    CHECK_EQ(rb.ReadOrSilence(out, sizeof(out)), static_cast<size_t>(16));
    for (size_t i = 0; i < 16; ++i) {
        CHECK_EQ(out[i], first[i]);
    }
    for (size_t i = 16; i < sizeof(out); ++i) {
        CHECK_EQ(out[i], static_cast<uint8_t>(0));
    }
    // 此时已欠载：FIFO 空。
    CHECK_EQ(rb.AvailableToRead(), static_cast<size_t>(0));

    // 阶段 2：生产者恢复供数（模拟解码线程继续写）。
    const std::vector<uint8_t> second = MakePattern(20, 200);
    CHECK_EQ(rb.Write(second.data(), second.size()), static_cast<size_t>(20));
    CHECK_EQ(rb.AvailableToRead(), static_cast<size_t>(20));

    // 阶段 3：消费者继续读，必须读到恢复后的新数据（而不是旧数据或错位数据）。
    uint8_t out2[20];
    CHECK_EQ(rb.Read(out2, sizeof(out2)), static_cast<size_t>(20));
    for (size_t i = 0; i < 20; ++i) {
        CHECK_EQ(out2[i], second[i]);
    }
    CHECK_EQ(rb.AvailableToRead(), static_cast<size_t>(0));
}

// T17 第 2 条（环绕场景）：欠载点恰好落在环绕边界后仍能正确恢复。
TEST_CASE(testRecoversAfterUnderrunAcrossWrap)
{
    slh::RingBuffer rb(16);

    // 先写 12 字节并读走 8 字节，使写指针停在 12、读指针停在 8。
    const std::vector<uint8_t> a = MakePattern(12, 0);
    CHECK_EQ(rb.Write(a.data(), a.size()), static_cast<size_t>(12));
    uint8_t tmp[8];
    CHECK_EQ(rb.Read(tmp, sizeof(tmp)), static_cast<size_t>(8));

    // 读空剩余 4 字节 → 欠载。
    uint8_t drain[4];
    CHECK_EQ(rb.Read(drain, sizeof(drain)), static_cast<size_t>(4));
    CHECK_EQ(rb.AvailableToRead(), static_cast<size_t>(0));

    // 恢复写入 16 字节，必然跨过环绕点。
    const std::vector<uint8_t> b = MakePattern(16, 50);
    CHECK_EQ(rb.Write(b.data(), b.size()), static_cast<size_t>(16));

    uint8_t out[16];
    CHECK_EQ(rb.Read(out, sizeof(out)), static_cast<size_t>(16));
    for (size_t i = 0; i < 16; ++i) {
        CHECK_EQ(out[i], b[i]);
    }
}

} // namespace

int main()
{
    std::printf("== RingBufferTests ==\n");
    testUnderrunWritesSilence_runner();
    testReadOrSilenceOnEmptyBuffer_runner();
    testWriteClampsToCapacity_runner();
    testWrapAround_runner();
    testClearReleasesAll_runner();
    testZeroCapacityIsSafe_runner();
    testTwoSecondCapacityMath_runner();
    testRecoversAfterUnderrun_runner();
    testRecoversAfterUnderrunAcrossWrap_runner();
    return Summarize("RingBufferTests");
}
