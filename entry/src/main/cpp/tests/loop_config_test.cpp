/*
 * LoopConfig 单测：覆盖 T02 / T03 / T04。
 *
 * 全部为纯逻辑断言，不依赖音频文件与真机。
 */

#include "loop_config.h"
#include "test_common.h"

namespace {

// 总帧数取合成 fixture 的 LoopEnd，便于与后续任务对照。
constexpr int64_t kTotal = 121275;

struct ErrorRecord {
    int count = 0;
    slh::LoopConfigError last = slh::LoopConfigError::kNone;
};

void RecordError(slh::LoopConfigError error, void* userData)
{
    auto* record = static_cast<ErrorRecord*>(userData);
    ++record->count;
    record->last = error;
}

// 新实例默认区间必须是满区间 [0, Total)，否则 T12 的「从头播放」不成立。
TEST_CASE(testDefaultRangeCoversWholeFile)
{
    slh::LoopConfig cfg(kTotal);
    CHECK_EQ(cfg.TotalFrames(), kTotal);
    CHECK_EQ(cfg.Start(), static_cast<int64_t>(0));
    CHECK_EQ(cfg.End(), kTotal);

    // 非法总帧数按 0 处理，不得产生负区间。
    slh::LoopConfig empty(-5);
    CHECK_EQ(empty.TotalFrames(), static_cast<int64_t>(0));
    CHECK_EQ(empty.Start(), static_cast<int64_t>(0));
    CHECK_EQ(empty.End(), static_cast<int64_t>(0));
}

// 合法区间必须原样生效。
TEST_CASE(testValidRangeApplied)
{
    slh::LoopConfig cfg(kTotal);
    CHECK_TRUE(cfg.SetRange(33075, 90000) == slh::LoopConfigError::kNone);
    CHECK_EQ(cfg.Start(), static_cast<int64_t>(33075));
    CHECK_EQ(cfg.End(), static_cast<int64_t>(90000));
}

// T02：终点为 0、负数、超过 Total 时归一化为 Total，且不得拒绝设置。
TEST_CASE(testInvalidEndNormalized)
{
    ErrorRecord record;

    struct Case {
        int64_t end;
    };
    const Case cases[] = {{0}, {-1}, {-4096}, {kTotal + 1}, {kTotal * 2}};

    for (const Case& c : cases) {
        slh::LoopConfig cfg(kTotal);
        cfg.SetErrorCallback(RecordError, &record);

        CHECK_TRUE(cfg.SetRange(33075, c.end) == slh::LoopConfigError::kNone);
        // 归一化后终点必须是 Total，起点保持不变。
        CHECK_EQ(cfg.Start(), static_cast<int64_t>(33075));
        CHECK_EQ(cfg.End(), kTotal);
    }

    // 归一化是成功路径，不应触发错误回调（T02 要求不中断）。
    CHECK_EQ(record.count, 0);
}

// T02 边界：终点恰为 Total 或 Total-1 时保持原值。
TEST_CASE(testEndBoundaryKeepsValue)
{
    slh::LoopConfig cfg(kTotal);
    CHECK_TRUE(cfg.SetRange(1000, kTotal) == slh::LoopConfigError::kNone);
    CHECK_EQ(cfg.End(), kTotal);

    CHECK_TRUE(cfg.SetRange(1000, kTotal - 1) == slh::LoopConfigError::kNone);
    CHECK_EQ(cfg.End(), kTotal - 1);
}

// T03：起点为负数或 >= Total 时拒绝，保持原区间并上报错误。
TEST_CASE(testInvalidStartRejected)
{
    slh::LoopConfig cfg(kTotal);
    CHECK_TRUE(cfg.SetRange(33075, 90000) == slh::LoopConfigError::kNone);

    ErrorRecord record;
    cfg.SetErrorCallback(RecordError, &record);

    const int64_t invalidStarts[] = {-1, -kTotal, kTotal, kTotal + 1};
    for (int64_t start : invalidStarts) {
        CHECK_TRUE(cfg.SetRange(start, 100000) == slh::LoopConfigError::kInvalidStart);
        // 原区间必须保持不变。
        CHECK_EQ(cfg.Start(), static_cast<int64_t>(33075));
        CHECK_EQ(cfg.End(), static_cast<int64_t>(90000));
        CHECK_TRUE(record.last == slh::LoopConfigError::kInvalidStart);
    }
    CHECK_EQ(record.count, 4);
}

// T04：终点 <= 起点时拒绝，保持原区间，且不得进入空区间。
TEST_CASE(testDegenerateRangeRejected)
{
    slh::LoopConfig cfg(kTotal);
    CHECK_TRUE(cfg.SetRange(33075, 90000) == slh::LoopConfigError::kNone);

    ErrorRecord record;
    cfg.SetErrorCallback(RecordError, &record);

    // 相等与倒置两种退化形态。
    CHECK_TRUE(cfg.SetRange(50000, 50000) == slh::LoopConfigError::kDegenerateRange);
    CHECK_EQ(cfg.Start(), static_cast<int64_t>(33075));
    CHECK_EQ(cfg.End(), static_cast<int64_t>(90000));

    CHECK_TRUE(cfg.SetRange(50000, 40000) == slh::LoopConfigError::kDegenerateRange);
    CHECK_EQ(cfg.Start(), static_cast<int64_t>(33075));
    CHECK_EQ(cfg.End(), static_cast<int64_t>(90000));

    CHECK_TRUE(cfg.SetRange(40000, 1) == slh::LoopConfigError::kDegenerateRange);
    CHECK_EQ(cfg.Start(), static_cast<int64_t>(33075));
    CHECK_EQ(cfg.End(), static_cast<int64_t>(90000));

    CHECK_EQ(record.count, 3);
    CHECK_TRUE(record.last == slh::LoopConfigError::kDegenerateRange);
}

// 失败后区间仍必须可用：随后的合法设置不能被历史失败影响。
TEST_CASE(testValidSetAfterRejection)
{
    slh::LoopConfig cfg(kTotal);
    CHECK_TRUE(cfg.SetRange(-1, 100) == slh::LoopConfigError::kInvalidStart);
    CHECK_TRUE(cfg.SetRange(8000, 8000) == slh::LoopConfigError::kDegenerateRange);

    // 失败后仍是满区间。
    CHECK_EQ(cfg.Start(), static_cast<int64_t>(0));
    CHECK_EQ(cfg.End(), kTotal);

    CHECK_TRUE(cfg.SetRange(500, kTotal) == slh::LoopConfigError::kNone);
    CHECK_EQ(cfg.Start(), static_cast<int64_t>(500));
    CHECK_EQ(cfg.End(), kTotal);
}

// 重新加载文件时，区间回到新文件的满区间。
TEST_CASE(testResetRestoresFullRange)
{
    slh::LoopConfig cfg(kTotal);
    CHECK_TRUE(cfg.SetRange(33075, 90000) == slh::LoopConfigError::kNone);

    cfg.Reset(132000);
    CHECK_EQ(cfg.TotalFrames(), static_cast<int64_t>(132000));
    CHECK_EQ(cfg.Start(), static_cast<int64_t>(0));
    CHECK_EQ(cfg.End(), static_cast<int64_t>(132000));
}

// 未登记回调时拒绝路径不得崩溃。
TEST_CASE(testRejectionWithoutCallback)
{
    slh::LoopConfig cfg(kTotal);
    cfg.SetErrorCallback(nullptr, nullptr);
    CHECK_TRUE(cfg.SetRange(-1, 10) == slh::LoopConfigError::kInvalidStart);
    CHECK_TRUE(cfg.SetRange(10, 10) == slh::LoopConfigError::kDegenerateRange);
    CHECK_EQ(cfg.Start(), static_cast<int64_t>(0));
    CHECK_EQ(cfg.End(), kTotal);
}

} // namespace

int main()
{
    std::printf("== LoopConfigTests ==\n");
    testDefaultRangeCoversWholeFile_runner();
    testValidRangeApplied_runner();
    testInvalidEndNormalized_runner();
    testEndBoundaryKeepsValue_runner();
    testInvalidStartRejected_runner();
    testDegenerateRangeRejected_runner();
    testValidSetAfterRejection_runner();
    testResetRestoresFullRange_runner();
    testRejectionWithoutCallback_runner();
    return Summarize("LoopConfigTests");
}
