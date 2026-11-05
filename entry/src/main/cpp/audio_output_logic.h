/*
 * AudioOutput 中与 OHAudio 无关的纯逻辑。
 *
 * 抽出来的目的：写回调的正确性（帧数换算、欠载填充、静音边界）是本变更
 * 最容易被忽视的缺陷来源，而 AudioOutput 本身依赖 OHAudio 无法在开发机
 * 单测。把判定逻辑放进这里，就能用普通 C++ 单测覆盖。
 */

#ifndef SEAMLESS_LOOP_AUDIO_OUTPUT_LOGIC_H
#define SEAMLESS_LOOP_AUDIO_OUTPUT_LOGIC_H

#include <cstdint>
#include <cstring>

namespace slh {

struct WritePlan {
    // 应从 FIFO 读取的字节数（按 BlockAlign 取整）。
    int32_t bytesFromFifo;
    // 需要补静音的字节数，已包含因对齐被舍弃的尾部字节。
    int32_t silenceBytes;
    // 因 BlockAlign 对齐而被舍弃的尾部字节数（通常为 0）。
    // 注意：这是 silenceBytes 的子集，仅用于诊断，不参与求和。
    int32_t droppedTailBytes;
};

// 规划一次写回调的数据分配。
//
// audioDataSize 由 OHAudio 给出，正常是 BlockAlign 的整数倍；但防御性地
// 处理非整数倍情况：整块部分从 FIFO 取，尾部余数字节补静音。
//
// 守恒关系：bytesFromFifo + silenceBytes == audioDataSize。
// droppedTailBytes 是 silenceBytes 的子集，不得重复计入。
inline WritePlan PlanWrite(int32_t audioDataSize, int32_t blockAlign, size_t availableBytes)
{
    WritePlan plan{0, 0, 0};
    if (audioDataSize <= 0) {
        return plan;
    }
    if (blockAlign <= 0) {
        // 配置异常：整块视为静音，避免越界读。
        plan.silenceBytes = audioDataSize;
        return plan;
    }

    const int32_t alignedBytes = audioDataSize - (audioDataSize % blockAlign);
    plan.droppedTailBytes = audioDataSize - alignedBytes;

    const size_t fromFifo = availableBytes < static_cast<size_t>(alignedBytes)
                                ? availableBytes - (availableBytes % static_cast<size_t>(blockAlign))
                                : static_cast<size_t>(alignedBytes);

    plan.bytesFromFifo = static_cast<int32_t>(fromFifo);
    // 静音 = 整块中 FIFO 没提供的部分 + 尾部余数。
    plan.silenceBytes = alignedBytes - plan.bytesFromFifo + plan.droppedTailBytes;
    return plan;
}

// 是否构成一次「欠载」事件。
//
// 判定：本次回调需要的数据没有全部由 FIFO 提供。
// 播放刚开始、FIFO 尚未填充时也会命中——这是有意的：T17 要求
// 「缓冲欠载必须上报」，而不是只在特定阶段上报。
inline bool IsUnderrun(const WritePlan& plan)
{
    return plan.silenceBytes > 0;
}

} // namespace slh

#endif // SEAMLESS_LOOP_AUDIO_OUTPUT_LOGIC_H
