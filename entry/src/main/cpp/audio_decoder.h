/*
 * AudioDecoder：音频文件解码器。
 *
 * 职责边界：
 * - 只负责「打开文件 / 报告格式 / 按采样点 seek / 读出 S16 交错 PCM」。
 * - 不做缓冲、不做循环判断、不做线程管理，这些属于上层（解码线程 / RingBuffer）。
 *
 * 单位约定（对应 AGENTS.md 硬约束 2）：
 * - 对外一律用「采样点」，即 PCM 帧序号（int64），不是字节、不是毫秒、不是百分比。
 * - 立体声文件的一「帧」包含左右两个采样，帧序号与声道数无关。
 *
 * 对齐约定（对应 AGENTS.md 硬约束 3）：
 * - 输出格式固定为 S16LE 交错，因此一帧 = channels * 2 字节 = BlockAlign。
 * - 按帧 seek 天然满足 BlockAlign 对齐，不会撕裂声道。
 *
 * 本类不依赖 OHAudio / hilog，可在开发机上用 CMake + MSVC 直接单测。
 */

#ifndef SEAMLESS_LOOP_AUDIO_DECODER_H
#define SEAMLESS_LOOP_AUDIO_DECODER_H

#include <cstdint>
#include <memory>

namespace slh {

enum class AudioFormat {
    kUnknown = 0,
    kWav,
    kMp3,
    kOgg,
};

struct AudioFormatInfo {
    AudioFormat format = AudioFormat::kUnknown;
    int32_t sampleRate = 0;
    int32_t channels = 0;
    int64_t totalFrames = 0;

    // 一帧的字节数（= BlockAlign）。解码输出固定 S16LE 交错。
    int32_t bytesPerFrame() const
    {
        return channels * static_cast<int32_t>(sizeof(int16_t));
    }
};

class AudioDecoder {
public:
    AudioDecoder();
    ~AudioDecoder();

    AudioDecoder(const AudioDecoder&) = delete;
    AudioDecoder& operator=(const AudioDecoder&) = delete;

    // 打开文件并读取格式信息。支持 .wav / .mp3 / .ogg（按扩展名分派）。
    // 失败返回 false，不抛出异常。
    bool Open(const char* path);

    void Close();

    bool IsOpen() const;

    // Open 成功后有效。
    const AudioFormatInfo& Format() const;

    // 按采样点（帧）跳转。请求值会被 clamp 到 [0, totalFrames]。
    // 返回实际落点帧号；失败返回 -1。
    int64_t SeekToFrame(int64_t frame);

    // 读取最多 frames 帧到 out（S16 交错，容量需 >= frames * channels）。
    // 返回实际读到的帧数，0 表示已到末尾。
    int64_t ReadFrames(int16_t* out, int64_t frames);

    // 当前解码位置（帧）。
    int64_t CurrentFrame() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace slh

#endif // SEAMLESS_LOOP_AUDIO_DECODER_H
