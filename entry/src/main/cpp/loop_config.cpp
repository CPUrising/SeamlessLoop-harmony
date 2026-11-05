/*
 * LoopConfig 实现。校验顺序：先起点（T03），再归一化终点（T02），最后退化保护（T04）。
 */

#include "loop_config.h"

namespace slh {

LoopConfig::LoopConfig(int64_t totalFrames)
{
    Reset(totalFrames);
}

void LoopConfig::Reset(int64_t totalFrames)
{
    totalFrames_ = totalFrames > 0 ? totalFrames : 0;
    start_ = 0;
    end_ = totalFrames_;
}

LoopConfigError LoopConfig::SetRange(int64_t start, int64_t end)
{
    // T03：起点越界直接拒绝，不做任何猜测性修正。
    if (start < 0 || start >= totalFrames_) {
        Report(LoopConfigError::kInvalidStart);
        return LoopConfigError::kInvalidStart;
    }

    // T02：终点为 0、负数或超过总帧数时归一化为 Total。
    const int64_t normalizedEnd = (end <= 0 || end > totalFrames_) ? totalFrames_ : end;

    // T04：归一化后不得退化，否则会进入空区间死循环。
    if (normalizedEnd <= start) {
        Report(LoopConfigError::kDegenerateRange);
        return LoopConfigError::kDegenerateRange;
    }

    start_ = start;
    end_ = normalizedEnd;
    return LoopConfigError::kNone;
}

void LoopConfig::SetErrorCallback(LoopErrorCallback callback, void* userData)
{
    errorCallback_ = callback;
    errorUserData_ = userData;
}

void LoopConfig::Report(LoopConfigError error)
{
    if (errorCallback_ != nullptr) {
        errorCallback_(error, errorUserData_);
    }
}

} // namespace slh
