/*
 * LoopConfig：循环区间的校验与归一化。
 *
 * 对应任务 4.1 与 spec 的 T02 / T03 / T04：
 * - T02：LoopEnd 为 0、负数或大于总采样数时，归一化为 Total，且不拒绝该设置；
 * - T03：LoopStart 为负数或 >= Total 时拒绝，保持原循环起点，并通过错误回调上报；
 * - T04：归一化后 LoopEnd <= LoopStart 时拒绝，保持原循环区间不变。
 *
 * 单位一律为采样帧（int64），与 design.md 决策 4 一致；本类不涉及字节换算。
 * 纯逻辑、无锁、不依赖 OHAudio，可在开发机上单测。
 */

#ifndef SEAMLESS_LOOP_LOOP_CONFIG_H
#define SEAMLESS_LOOP_LOOP_CONFIG_H

#include <cstdint>

namespace slh {

// 循环点设置失败原因。
enum class LoopConfigError {
    kNone = 0,
    kInvalidStart,    // LoopStart < 0 或 >= Total
    kDegenerateRange, // 归一化后 LoopEnd <= LoopStart
};

// 错误回调：业务层据此上报失败原因（T03 要求）。
// 在 SetRange 的调用线程上同步触发，回调内不得再调用 SetRange。
using LoopErrorCallback = void (*)(LoopConfigError error, void* userData);

class LoopConfig {
public:
    explicit LoopConfig(int64_t totalFrames = 0);

    // 按新的音频总帧数重置循环区间为满区间 [0, Total)。
    void Reset(int64_t totalFrames);

    // 设置循环区间。成功返回 kNone 并更新区间；
    // 失败时区间保持不变，并原样返回失败原因。
    LoopConfigError SetRange(int64_t start, int64_t end);

    int64_t Start() const { return start_; }
    int64_t End() const { return end_; }
    int64_t TotalFrames() const { return totalFrames_; }

    // 登记错误回调（可为 nullptr），失败时同步触发。
    void SetErrorCallback(LoopErrorCallback callback, void* userData);

private:
    void Report(LoopConfigError error);

    int64_t totalFrames_ = 0;
    int64_t start_ = 0;
    int64_t end_ = 0;
    LoopErrorCallback errorCallback_ = nullptr;
    void* errorUserData_ = nullptr;
};

} // namespace slh

#endif // SEAMLESS_LOOP_LOOP_CONFIG_H
