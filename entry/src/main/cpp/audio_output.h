/*
 * AudioOutput：OHAudio（NDK）渲染器封装。
 *
 * 数据通路（design.md 决策 2 / 3）：
 *   解码线程 → RingBuffer → OHAudio 写回调
 * 写回调内只做内存拷贝，不做文件 IO、不做解码、不加锁。
 *
 * 本阶段（任务 3.5）已接入 FIFO；循环跳转在任务 4.2 由解码线程负责，
 * 因此回调侧完全不知道"循环"的存在。
 *
 * 注意：本文件依赖 OHAudio，只能在 OHOS 工具链下编译，无法在开发机上单测。
 * 可测的逻辑（欠载判定、帧数换算）已抽到 audio_output_logic.h。
 */

#ifndef SEAMLESS_LOOP_AUDIO_OUTPUT_H
#define SEAMLESS_LOOP_AUDIO_OUTPUT_H

#include <atomic>
#include <cstdint>
#include <mutex>

#include <ohaudio/native_audiostream_base.h>
#include <ohaudio/native_audiorenderer.h>
#include <ohaudio/native_audiostreambuilder.h>

#include "ring_buffer.h"

namespace slh {

// 冒烟音参数：48kHz / 双声道 / S16LE，BlockAlign = 2 * 2 = 4 字节。
constexpr int32_t kToneSampleRate = 48000;
constexpr int32_t kToneChannelCount = 2;
constexpr int32_t kToneBytesPerSample = 2;
constexpr int32_t kToneBlockAlign = kToneChannelCount * kToneBytesPerSample;
constexpr double kToneFrequencyHz = 440.0;
constexpr int64_t kToneDurationFrames = kToneSampleRate; // 1 秒

// 纯函数：把 [startFrame, startFrame + frameCount) 区间的正弦波写入 out。
// 超出 kToneDurationFrames 的部分写静音。返回实际写入的帧数。
int32_t FillToneFrames(int16_t* out, int32_t frameCount, int64_t startFrame);

// 欠载事件回调。在 OHAudio 回调线程上触发，实现方必须极快返回。
using UnderrunCallback = void (*)(void* userData, int64_t underrunBytes);

class AudioOutput {
public:
    AudioOutput() = default;
    ~AudioOutput();

    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    // 设置数据源。必须在 Start 之前调用；Start 之后更换需先 Stop。
    void SetRingBuffer(RingBuffer* ring);

    // 配置渲染器格式（采样率 / 声道数）。必须在 Start 之前调用；
    // 若渲染器已存在会先释放，避免格式与已建流不一致。
    // 返回 0 表示成功，非 0 为参数错误。
    int32_t Configure(int32_t sampleRate, int32_t channels);

    int32_t SampleRate() const;
    int32_t Channels() const;
    // 一帧字节数 = 声道数 × 2（S16LE）。
    int32_t BlockAlign() const;

    // 设置欠载事件回调（可选）。
    void SetUnderrunCallback(UnderrunCallback cb, void* userData);

    // 构建（首次）并启动渲染器。
    // 返回 0 表示成功，非 0 为 OHAudio 错误码。
    int32_t Start();

    // 停止并释放渲染器。可重复调用。
    void Stop();

    bool IsRunning() const;

    // 累计欠载字节数（诊断用）。
    int64_t UnderrunBytes() const;

    // 供 G0 冒烟使用的兼容入口：Start 后以正弦波作为数据源。
    int32_t StartTone();
    void StopTone() { Stop(); }

private:
    static OH_AudioData_Callback_Result OnWriteData(OH_AudioRenderer* renderer, void* userData,
                                                    void* audioData, int32_t audioDataSize);

    int32_t EnsureRendererLocked();
    void ReleaseRendererLocked();

    std::mutex mutex_;
    OH_AudioStreamBuilder* builder_ = nullptr;
    OH_AudioRenderer* renderer_ = nullptr;

    // 数据源二选一：ring_ 优先；ring_ 为空时退回正弦波（G0 冒烟）。
    std::atomic<RingBuffer*> ring_{nullptr};
    std::atomic<bool> toneMode_{false};
    std::atomic<int64_t> toneFramePos_{0};

    // 渲染器格式。默认取冒烟音参数，加载真实文件时由 Configure 覆盖。
    std::atomic<int32_t> sampleRate_{kToneSampleRate};
    std::atomic<int32_t> channels_{kToneChannelCount};

    std::atomic<UnderrunCallback> underrunCb_{nullptr};
    std::atomic<void*> underrunUserData_{nullptr};
    std::atomic<int64_t> underrunBytes_{0};
    std::atomic<bool> running_{false};
};

} // namespace slh

#endif // SEAMLESS_LOOP_AUDIO_OUTPUT_H
