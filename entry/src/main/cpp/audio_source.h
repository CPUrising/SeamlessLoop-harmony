/*
 * AudioSource：虚拟时间轴数据源（对应 design.md 决策 3）。
 *
 * 职责边界：
 * - 对外暴露与 AudioDecoder 相同的 Format / SeekToFrame / ReadFrames / CurrentFrame，
 *   让 DecodeThread 的循环内核无需感知「单文件」与「A/B 双文件」的差异。
 * - 内部持有 1 个或 2 个 AudioDecoder，负责把虚拟轴坐标映射到具体文件。
 *
 * 坐标映射（A 帧数记为 NA，B 帧数记为 NB）：
 * - TotalFrames = NA + NB。
 * - 0 <= p < NA       -> A 的第 p 帧
 * - NA <= p < NA + NB -> B 的第 p - NA 帧
 *
 * 关键约定：
 * - LoopStart / LoopEnd 由业务层按虚拟轴传入，本类不做任何坐标变换，
 *   因此 DecodeThread 的循环跳转逻辑完全不用改。
 * - ReadFrames 读到 A 末尾会自动切到 B 继续读，不返回 0（除非已到虚拟轴末尾）。
 * - 采样率与声道数取 A 的值；B 与 A 不一致时拒绝配对，避免输出错位音频。
 *
 * 本类不依赖 OHAudio，可在开发机上单测。
 */

#ifndef SEAMLESS_LOOP_AUDIO_SOURCE_H
#define SEAMLESS_LOOP_AUDIO_SOURCE_H

#include <cstdint>
#include <memory>

#include "audio_decoder.h"

namespace slh {

class AudioSource {
public:
    AudioSource();
    ~AudioSource();

    AudioSource(const AudioSource&) = delete;
    AudioSource& operator=(const AudioSource&) = delete;

    // 单文件模式。失败返回 false。
    bool Open(const char* path);

    // A/B 双文件模式：A 在前、B 在后串成一条虚拟时间轴。
    // 两个文件的采样率与声道数必须一致，否则返回 false。
    bool OpenPair(const char* aPath, const char* bPath);

    void Close();

    bool IsOpen() const;

    // 是否为 A/B 双文件模式。
    bool IsPair() const;

    // Open / OpenPair 成功后有效。totalFrames 为虚拟轴总长。
    const AudioFormatInfo& Format() const;

    // 按虚拟轴位置（帧）跳转。请求值 clamp 到 [0, totalFrames]。
    // 返回实际落点帧号；失败返回 -1。
    int64_t SeekToFrame(int64_t frame);

    // 读取最多 frames 帧到 out（S16 交错，容量需 >= frames * channels）。
    // 跨文件时自动续读；返回实际读到的帧数，0 表示已到虚拟轴末尾。
    int64_t ReadFrames(int16_t* out, int64_t frames);

    // 当前虚拟轴位置（帧）。
    int64_t CurrentFrame() const;

private:
    // 当前生效的解码器（单文件模式恒为 A）。
    AudioDecoder* ActiveDecoder() const;

    // 当前文件读完后切到下一个文件。没有下一个文件时返回 false。
    bool SwitchToNextFile();

    std::unique_ptr<AudioDecoder> aDecoder_;
    std::unique_ptr<AudioDecoder> bDecoder_;

    AudioFormatInfo info_;
    int64_t aFrames_ = 0;   // A 的帧数，即虚拟轴上 A/B 的分界点
    bool isPair_ = false;
    int32_t activeIndex_ = 0; // 0 = A，1 = B
    int64_t currentFrame_ = 0;
};

} // namespace slh

#endif // SEAMLESS_LOOP_AUDIO_SOURCE_H
