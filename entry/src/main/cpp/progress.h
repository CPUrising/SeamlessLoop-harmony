/*
 * 播放进度计算（对应 design.md 决策 6，覆盖 T10 / T11）。
 *
 * 单位：采样帧（int64），与循环点一致。
 *
 * 为什么需要回绕补偿：
 * 解码位置是「文件内位置」，循环跳转时会从 LoopEnd 掉回 LoopStart，
 * 于是「解码位置 − FIFO 未消费帧数」在边界处会回退一个循环长度。
 * 业务层需要的是单调递增的播放时间轴，所以每次跳转都要补回一个循环长度。
 *
 * 为什么 Intro 阶段不能补偿（T11）：
 * LoopStart 可能大于 0。此时解码位置尚未进入循环体，若提前补偿，
 * 进度条会凭空多出一个循环长度。
 *
 * 纯函数、无状态，可在开发机上单测。
 */

#ifndef SEAMLESS_LOOP_PROGRESS_H
#define SEAMLESS_LOOP_PROGRESS_H

#include <cstdint>

namespace slh {

// 把「文件内解码位置」换算成「已补偿的播放时间轴位置」。
// loopsSinceSeek 为自上次 seek / 循环点变更以来的跳转次数：
// - 0：仍在 Intro 或循环体首轮，直接返回真实解码位置（T11）；
// - n>0：每跳转一次补一个循环长度（T10）。
inline int64_t ComputeCompensatedDecodeFrame(int64_t decodedFrame, int64_t loopStart,
                                             int64_t loopEnd, int64_t loopsSinceSeek)
{
    if (loopsSinceSeek <= 0) {
        return decodedFrame;
    }
    const int64_t loopLength = loopEnd - loopStart;
    if (loopLength <= 0) {
        return decodedFrame;
    }
    return decodedFrame + loopLength * loopsSinceSeek;
}

// 由已补偿的解码位置推算扬声器位置：减去 FIFO 中尚未被消费的帧数，并夹到非负。
inline int64_t ComputePlaybackFrame(int64_t compensatedDecodeFrame, int64_t bufferedFrames)
{
    const int64_t position = compensatedDecodeFrame - bufferedFrames;
    return position > 0 ? position : 0;
}

// 便捷入口：一次算完补偿与缓冲扣减。
inline int64_t ComputePlaybackFrame(int64_t decodedFrame, int64_t bufferedFrames,
                                   int64_t loopStart, int64_t loopEnd,
                                   int64_t loopsSinceSeek)
{
    return ComputePlaybackFrame(
        ComputeCompensatedDecodeFrame(decodedFrame, loopStart, loopEnd, loopsSinceSeek),
        bufferedFrames);
}

} // namespace slh

#endif // SEAMLESS_LOOP_PROGRESS_H
