/*
 * AbPair 实现：纯字符串处理 + 文件存在性检查。
 */

#include "ab_pair.h"

#include <sys/stat.h>

namespace slh {
namespace {

// 一组后缀：A 侧与 B 侧。
struct SuffixPair {
    const char* a;
    const char* b;
};

// 三组后缀，全部小写；比较时把输入统一转小写。
constexpr SuffixPair kSuffixPairs[] = {
    {"_a", "_b"},
    {"_start", "_end"},
};

char ToLower(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return static_cast<char>(c - 'A' + 'a');
    }
    return c;
}

// 判断 s 是否以 suffix 结尾（大小写不敏感）。
bool EndsWithIgnoreCase(const std::string& s, const char* suffix)
{
    const size_t suffixLen = std::char_traits<char>::length(suffix);
    if (s.size() < suffixLen) {
        return false;
    }
    const size_t offset = s.size() - suffixLen;
    for (size_t i = 0; i < suffixLen; ++i) {
        if (ToLower(s[offset + i]) != suffix[i]) {
            return false;
        }
    }
    return true;
}

// 判断 s 的末尾 suffixLen 个字符是否全为大写字母。
// 用于让替换后的后缀保持输入的大小写风格：_A -> _B，_a -> _b。
bool TailIsUpperCase(const std::string& s, size_t suffixLen)
{
    if (s.size() < suffixLen) {
        return false;
    }
    const size_t offset = s.size() - suffixLen;
    for (size_t i = 0; i < suffixLen; ++i) {
        const char c = s[offset + i];
        if (c >= 'a' && c <= 'z') {
            return false;
        }
    }
    return true;
}

// 按 upper 决定返回大写还是小写形式的后缀。
std::string CaseAdjusted(const char* suffix, bool upper)
{
    std::string out = suffix;
    if (upper) {
        for (char& c : out) {
            if (c >= 'a' && c <= 'z') {
                c = static_cast<char>(c - 'a' + 'A');
            }
        }
    }
    return out;
}

// 把路径拆成「目录 + 主干 + 扩展名」。
// 扩展名指最后一个 '.' 之后的部分（含 '.'）；没有 '.' 时 ext 为空。
struct PathParts {
    std::string dir;   // 含末尾分隔符，可能为空
    std::string stem;  // 不含扩展名
    std::string ext;   // 含 '.'，可能为空
};

PathParts SplitPath(const std::string& path)
{
    PathParts parts;

    // 先分离目录：找最后一个 '/' 或 '\\'。
    size_t sepPos = path.find_last_of("/\\");
    std::string fileName;
    if (sepPos == std::string::npos) {
        fileName = path;
    } else {
        parts.dir = path.substr(0, sepPos + 1);
        fileName = path.substr(sepPos + 1);
    }

    // 再分离扩展名：找文件名里最后一个 '.'。
    const size_t dotPos = fileName.find_last_of('.');
    if (dotPos == std::string::npos) {
        parts.stem = fileName;
    } else {
        parts.stem = fileName.substr(0, dotPos);
        parts.ext = fileName.substr(dotPos);
    }
    return parts;
}

bool FileExists(const std::string& path)
{
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

} // namespace

AbPairResult DetectAbPairByName(const std::string& path)
{
    AbPairResult result;
    if (path.empty()) {
        return result;
    }

    const PathParts parts = SplitPath(path);

    for (const SuffixPair& pair : kSuffixPairs) {
        const size_t aLen = std::char_traits<char>::length(pair.a);
        const size_t bLen = std::char_traits<char>::length(pair.b);

        // 输入是 A 侧：把主干末尾的 _a 换成 _b，拼回扩展名得到 B 侧路径。
        // 替换后的后缀沿用输入的大小写风格（_A -> _B，_a -> _b）。
        if (EndsWithIgnoreCase(parts.stem, pair.a)) {
            const bool upper = TailIsUpperCase(parts.stem, aLen);
            result.isPair = true;
            result.aPath = path;
            result.bPath = parts.dir + parts.stem.substr(0, parts.stem.size() - aLen) +
                           CaseAdjusted(pair.b, upper) + parts.ext;
            return result;
        }

        // 输入是 B 侧：把主干末尾的 _b 换成 _a，拼回扩展名得到 A 侧路径。
        if (EndsWithIgnoreCase(parts.stem, pair.b)) {
            const bool upper = TailIsUpperCase(parts.stem, bLen);
            result.isPair = true;
            result.aPath = parts.dir + parts.stem.substr(0, parts.stem.size() - bLen) +
                           CaseAdjusted(pair.a, upper) + parts.ext;
            result.bPath = path;
            return result;
        }
    }

    return result;
}

AbPairResult DetectAbPair(const std::string& path)
{
    AbPairResult result = DetectAbPairByName(path);
    if (!result.isPair) {
        return result;
    }

    // 配对文件必须真实存在，否则退化为单文件播放（P02）。
    if (!FileExists(result.aPath) || !FileExists(result.bPath)) {
        return AbPairResult{};
    }
    return result;
}

} // namespace slh
