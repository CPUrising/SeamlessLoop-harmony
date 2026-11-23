/*
 * AbPair：A/B 双文件配对识别。
 *
 * 原项目用两个文件表示一首曲目：A 段（前奏）+ B 段（循环体）。
 * 播放时 A 播完无缝接 B，之后循环 B。数据库只存 A 的记录，B 由文件名推导。
 *
 * 配对规则（大小写不敏感，仅同目录、同扩展名）：
 *   _a / _b
 *   _A / _B
 *   _start / _end
 *
 * 输入可以是 A 侧或 B 侧，统一归一化为「A 侧路径 + B 侧路径」。
 * 找不到配对时返回 false，调用方退化为单文件播放。
 *
 * 本类只做字符串与文件存在性判断，不依赖解码器，可在开发机上单测。
 */

#ifndef SEAMLESS_LOOP_AB_PAIR_H
#define SEAMLESS_LOOP_AB_PAIR_H

#include <string>

namespace slh {

struct AbPairResult {
    // 是否为 A/B 对。false 时 aPath / bPath 均为空。
    bool isPair = false;
    // 归一化后的 A 侧路径（前奏）。
    std::string aPath;
    // 归一化后的 B 侧路径（循环体）。
    std::string bPath;
};

// 判断 path 是否属于 A/B 对，并返回归一化结果。
// 只有「配对文件确实存在」时才返回 isPair = true。
AbPairResult DetectAbPair(const std::string& path);

// 判断 path 是否属于 A/B 对，但不检查文件是否存在。
// 供单测与「只想知道命名形态」的场景使用。
AbPairResult DetectAbPairByName(const std::string& path);

} // namespace slh

#endif // SEAMLESS_LOOP_AB_PAIR_H
