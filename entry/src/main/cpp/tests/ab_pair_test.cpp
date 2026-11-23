/*
 * AbPair 单测：覆盖 P01（三组后缀配对）、P02（找不到配对退化）、P03（非 A/B 文件）。
 *
 * 只测纯字符串逻辑（DetectAbPairByName），不依赖真实文件；
 * 文件存在性检查（DetectAbPair）用 fixtures/synthetic 下的合成 A/B 对验证。
 */

#include "ab_pair.h"
#include "test_common.h"

#include <string>

#ifndef FIXTURE_DIR
#error "FIXTURE_DIR 未定义：请在 CMake 中通过 -DFIXTURE_DIR=... 注入"
#endif

namespace {

std::string FixturePath(const char* name)
{
    std::string p = FIXTURE_DIR;
    if (!p.empty() && p.back() != '/' && p.back() != '\\') {
        p += '/';
    }
    p += name;
    return p;
}

// P01：小写 _a / _b 配对，输入 A 侧。
TEST_CASE(testLowercasePairFromA)
{
    const slh::AbPairResult r = slh::DetectAbPairByName("/music/bgm03_2_a.ogg");
    CHECK_TRUE(r.isPair);
    CHECK_TRUE(r.aPath == "/music/bgm03_2_a.ogg");
    CHECK_TRUE(r.bPath == "/music/bgm03_2_b.ogg");
}

// P01：小写 _a / _b 配对，输入 B 侧，必须归一化为 A 侧在前。
TEST_CASE(testLowercasePairFromB)
{
    const slh::AbPairResult r = slh::DetectAbPairByName("/music/bgm03_2_b.ogg");
    CHECK_TRUE(r.isPair);
    CHECK_TRUE(r.aPath == "/music/bgm03_2_a.ogg");
    CHECK_TRUE(r.bPath == "/music/bgm03_2_b.ogg");
}

// P01：大写 _A / _B 配对。
TEST_CASE(testUppercasePair)
{
    const slh::AbPairResult fromA = slh::DetectAbPairByName("/music/BGM_016_A.ogg");
    CHECK_TRUE(fromA.isPair);
    CHECK_TRUE(fromA.aPath == "/music/BGM_016_A.ogg");
    CHECK_TRUE(fromA.bPath == "/music/BGM_016_B.ogg");

    const slh::AbPairResult fromB = slh::DetectAbPairByName("/music/BGM_016_B.ogg");
    CHECK_TRUE(fromB.isPair);
    CHECK_TRUE(fromB.aPath == "/music/BGM_016_A.ogg");
    CHECK_TRUE(fromB.bPath == "/music/BGM_016_B.ogg");
}

// P01：_start / _end 语义后缀。
TEST_CASE(testStartEndPair)
{
    const slh::AbPairResult fromStart = slh::DetectAbPairByName("/music/song_start.mp3");
    CHECK_TRUE(fromStart.isPair);
    CHECK_TRUE(fromStart.aPath == "/music/song_start.mp3");
    CHECK_TRUE(fromStart.bPath == "/music/song_end.mp3");

    const slh::AbPairResult fromEnd = slh::DetectAbPairByName("/music/song_end.mp3");
    CHECK_TRUE(fromEnd.isPair);
    CHECK_TRUE(fromEnd.aPath == "/music/song_start.mp3");
    CHECK_TRUE(fromEnd.bPath == "/music/song_end.mp3");
}

// P01：后缀匹配大小写不敏感，替换时保持输入的大小写风格。
// 规范只要求「匹配」不敏感，未要求混搭大小写配对（_A 配 _b），
// 因此纯字符串函数按输入风格生成对侧路径：_A -> _B，_a -> _b。
TEST_CASE(testCaseInsensitiveMatching)
{
    const slh::AbPairResult upper = slh::DetectAbPairByName("/music/track_A.ogg");
    CHECK_TRUE(upper.isPair);
    CHECK_TRUE(upper.aPath == "/music/track_A.ogg");
    CHECK_TRUE(upper.bPath == "/music/track_B.ogg");

    const slh::AbPairResult lower = slh::DetectAbPairByName("/music/track_a.ogg");
    CHECK_TRUE(lower.isPair);
    CHECK_TRUE(lower.bPath == "/music/track_b.ogg");

    // 后缀内部大小写混写也必须能匹配；替换结果统一为小写。
    const slh::AbPairResult mixed = slh::DetectAbPairByName("/music/track_Start.mp3");
    CHECK_TRUE(mixed.isPair);
    CHECK_TRUE(mixed.aPath == "/music/track_Start.mp3");
    CHECK_TRUE(mixed.bPath == "/music/track_end.mp3");
}

// P01：扩展名必须保留，且后缀替换只发生在主干上。
TEST_CASE(testExtensionPreserved)
{
    const slh::AbPairResult r = slh::DetectAbPairByName("C:\\music\\bgm03_2_a.ogg");
    CHECK_TRUE(r.isPair);
    CHECK_TRUE(r.aPath == "C:\\music\\bgm03_2_a.ogg");
    CHECK_TRUE(r.bPath == "C:\\music\\bgm03_2_b.ogg");
}

// P03：不含 A/B 后缀的文件名不配对。
TEST_CASE(testNonPairNames)
{
    CHECK_FALSE(slh::DetectAbPairByName("/music/song.mp3").isPair);
    CHECK_FALSE(slh::DetectAbPairByName("/music/song_ab.ogg").isPair);
    CHECK_FALSE(slh::DetectAbPairByName("/music/a.ogg").isPair);
    CHECK_FALSE(slh::DetectAbPairByName("/music/b.ogg").isPair);
    CHECK_FALSE(slh::DetectAbPairByName("").isPair);
    // 后缀必须紧贴主干末尾，中间不能有其他字符
    CHECK_FALSE(slh::DetectAbPairByName("/music/song_a_extra.ogg").isPair);
}

// P02：命名像 A/B 对，但配对文件不存在时必须退化为单文件。
TEST_CASE(testMissingCounterpartDegradesToSingle)
{
    // 合成 fixture 里只有 _a 与 _b 成对存在；这里用一个不存在的名字。
    const slh::AbPairResult r = slh::DetectAbPair(FixturePath("no_such_pair_a.wav"));
    CHECK_FALSE(r.isPair);
    CHECK_TRUE(r.aPath.empty());
    CHECK_TRUE(r.bPath.empty());
}

// P01 + 文件存在性：合成 A/B 对必须被识别。
TEST_CASE(testExistingPairDetected)
{
    const slh::AbPairResult fromA = slh::DetectAbPair(FixturePath("ab_pair_44100_stereo_a.wav"));
    CHECK_TRUE(fromA.isPair);
    CHECK_TRUE(fromA.aPath == FixturePath("ab_pair_44100_stereo_a.wav"));
    CHECK_TRUE(fromA.bPath == FixturePath("ab_pair_44100_stereo_b.wav"));

    // 从 B 侧进入也必须归一化到同一结果。
    const slh::AbPairResult fromB = slh::DetectAbPair(FixturePath("ab_pair_44100_stereo_b.wav"));
    CHECK_TRUE(fromB.isPair);
    CHECK_TRUE(fromB.aPath == fromA.aPath);
    CHECK_TRUE(fromB.bPath == fromA.bPath);
}

// P03：单文件 fixture 不应被误判为 A/B 对。
TEST_CASE(testSingleFileNotPair)
{
    CHECK_FALSE(slh::DetectAbPair(FixturePath("intro_loop_44100_mono.wav")).isPair);
}

} // namespace

int main()
{
    std::printf("== AbPairTests ==\n");
    std::printf("  fixture dir: %s\n", FIXTURE_DIR);
    testLowercasePairFromA_runner();
    testLowercasePairFromB_runner();
    testUppercasePair_runner();
    testStartEndPair_runner();
    testCaseInsensitiveMatching_runner();
    testExtensionPreserved_runner();
    testNonPairNames_runner();
    testMissingCounterpartDegradesToSingle_runner();
    testExistingPairDetected_runner();
    testSingleFileNotPair_runner();
    return Summarize("AbPairTests");
}
