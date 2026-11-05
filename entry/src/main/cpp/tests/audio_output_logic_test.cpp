/*
 * AudioOutput 纯逻辑单测：PlanWrite / IsUnderrun。
 *
 * 这两条是写回调的正确性核心：帧数换算错会导致爆音或越界，
 * 欠载判定错会导致 T17 无法上报。
 */

#include "audio_output_logic.h"
#include "test_common.h"

#include <cstdint>

namespace {

constexpr int32_t kBlockAlign = 4; // 48kHz / 双声道 / S16

// FIFO 数据充足：整块都从 FIFO 取，不补静音。
TEST_CASE(testPlanWriteFullData)
{
    const slh::WritePlan p = slh::PlanWrite(1024, kBlockAlign, 999999);
    CHECK_EQ(p.bytesFromFifo, 1024);
    CHECK_EQ(p.silenceBytes, 0);
    CHECK_EQ(p.droppedTailBytes, 0);
    CHECK_FALSE(slh::IsUnderrun(p));
}

// FIFO 完全空：全部补静音。
TEST_CASE(testPlanWriteEmptyFifo)
{
    const slh::WritePlan p = slh::PlanWrite(1024, kBlockAlign, 0);
    CHECK_EQ(p.bytesFromFifo, 0);
    CHECK_EQ(p.silenceBytes, 1024);
    CHECK_TRUE(slh::IsUnderrun(p));
}

// FIFO 只有部分数据：能取多少取多少，余下补静音。
TEST_CASE(testPlanWritePartialData)
{
    const slh::WritePlan p = slh::PlanWrite(1024, kBlockAlign, 400);
    CHECK_EQ(p.bytesFromFifo, 400);
    CHECK_EQ(p.silenceBytes, 624);
    CHECK_TRUE(slh::IsUnderrun(p));
}

// FIFO 里的数据不是整块时，只能取整块部分，余数留在 FIFO 里。
TEST_CASE(testPlanWriteIgnoresUnalignedAvailable)
{
    // 401 字节可用，BlockAlign=4 → 只能取 400。
    const slh::WritePlan p = slh::PlanWrite(1024, kBlockAlign, 401);
    CHECK_EQ(p.bytesFromFifo, 400);
    CHECK_EQ(p.silenceBytes, 624);
    CHECK_EQ(p.bytesFromFifo % kBlockAlign, 0);
}

// OHAudio 给出的 audioDataSize 非 BlockAlign 整数倍时，尾部余数补静音。
TEST_CASE(testPlanWriteHandlesUnalignedRequest)
{
    const slh::WritePlan p = slh::PlanWrite(1026, kBlockAlign, 999999);
    CHECK_EQ(p.bytesFromFifo, 1024);
    CHECK_EQ(p.droppedTailBytes, 2);
    CHECK_EQ(p.silenceBytes, 2);
    // 两个有效分量必须覆盖整个请求，否则回调会留下未初始化内存。
    // droppedTailBytes 是 silenceBytes 的子集，不重复计入。
    CHECK_EQ(p.bytesFromFifo + p.silenceBytes, 1026);
}

// 防御性：audioDataSize <= 0 时不得产生任何读取。
TEST_CASE(testPlanWriteZeroOrNegativeSize)
{
    for (int32_t size : {0, -1, -1024}) {
        const slh::WritePlan p = slh::PlanWrite(size, kBlockAlign, 1024);
        CHECK_EQ(p.bytesFromFifo, 0);
        CHECK_EQ(p.silenceBytes, 0);
        CHECK_EQ(p.droppedTailBytes, 0);
        CHECK_FALSE(slh::IsUnderrun(p));
    }
}

// 防御性：blockAlign 非法时全部按静音处理，绝不越界读。
TEST_CASE(testPlanWriteInvalidBlockAlign)
{
    for (int32_t ba : {0, -4}) {
        const slh::WritePlan p = slh::PlanWrite(1024, ba, 1024);
        CHECK_EQ(p.bytesFromFifo, 0);
        CHECK_EQ(p.silenceBytes, 1024);
        CHECK_TRUE(slh::IsUnderrun(p));
    }
}

// 单声道 16bit（BlockAlign=2）也要正确。
TEST_CASE(testPlanWriteMonoBlockAlign)
{
    const slh::WritePlan p = slh::PlanWrite(1000, 2, 501);
    CHECK_EQ(p.bytesFromFifo, 500);
    CHECK_EQ(p.silenceBytes, 500);
    CHECK_EQ(p.bytesFromFifo % 2, 0);
}

// 分量守恒：bytesFromFifo + silenceBytes 必须等于请求字节数。
TEST_CASE(testPlanWriteConservation)
{
    const int32_t sizes[] = {4, 8, 100, 1024, 4096, 1026, 1027};
    const size_t avails[] = {0, 1, 3, 4, 100, 400, 401, 1023, 1024, 99999};
    for (int32_t size : sizes) {
        for (size_t avail : avails) {
            const slh::WritePlan p = slh::PlanWrite(size, kBlockAlign, avail);
            CHECK_EQ(p.bytesFromFifo + p.silenceBytes, size);
            CHECK_TRUE(p.bytesFromFifo >= 0);
            CHECK_TRUE(p.silenceBytes >= 0);
            CHECK_TRUE(p.bytesFromFifo <= size);
            CHECK_EQ(p.bytesFromFifo % kBlockAlign, 0);
            // 尾部字节数不得超过总静音量。
            CHECK_TRUE(p.droppedTailBytes >= 0);
            CHECK_TRUE(p.droppedTailBytes <= p.silenceBytes);
        }
    }
}

} // namespace

int main()
{
    std::printf("== AudioOutputLogicTests ==\n");
    testPlanWriteFullData_runner();
    testPlanWriteEmptyFifo_runner();
    testPlanWritePartialData_runner();
    testPlanWriteIgnoresUnalignedAvailable_runner();
    testPlanWriteHandlesUnalignedRequest_runner();
    testPlanWriteZeroOrNegativeSize_runner();
    testPlanWriteInvalidBlockAlign_runner();
    testPlanWriteMonoBlockAlign_runner();
    testPlanWriteConservation_runner();
    return Summarize("AudioOutputLogicTests");
}
