/*
 * AudioOutput：OHAudio（NDK）渲染器封装。
 *
 * 本阶段（任务 2.3）只做 G0 冒烟：用写回调输出 1 秒 440Hz 正弦波，
 * 用于证明「PCM 通道可用」。真正的解码 / 循环跳转在后续任务接入。
 *
 * 约束：
 * - 写回调内不做文件 IO、不做解码、不加锁（只读原子量 + 写内存）。
 * - 回调必须填满 audioDataSize 字节，否则会产生欠载杂音。
 */

#ifndef SEAMLESS_LOOP_AUDIO_OUTPUT_H
#define SEAMLESS_LOOP_AUDIO_OUTPUT_H

#include <atomic>
#include <cstdint>
#include <mutex>

#include <ohaudio/native_audiostream_base.h>
#include <ohaudio/native_audiorenderer.h>
#include <ohaudio/native_audiostreambuilder.h>

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
// 抽成纯函数是为了后续能在本机（非鸿蒙）做单元测试。
int32_t FillToneFrames(int16_t* out, int32_t frameCount, int64_t startFrame);

class AudioOutput {
public:
    AudioOutput() = default;
    ~AudioOutput();

    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    // 构建（首次）并启动渲染器，从头播放 1 秒正弦波。
    // 返回 0 表示成功，非 0 为 OHAudio 错误码。
    int32_t StartTone();

    // 停止并释放渲染器。可重复调用。
    void StopTone();

    bool IsRunning() const;

private:
    static OH_AudioData_Callback_Result OnWriteData(OH_AudioRenderer* renderer, void* userData,
                                                    void* audioData, int32_t audioDataSize);

    int32_t EnsureRendererLocked();
    void ReleaseRendererLocked();

    std::mutex mutex_;
    OH_AudioStreamBuilder* builder_ = nullptr;
    OH_AudioRenderer* renderer_ = nullptr;
    std::atomic<bool> running_{false};
    std::atomic<int64_t> toneFramePos_{0};
};

} // namespace slh

#endif // SEAMLESS_LOOP_AUDIO_OUTPUT_H
