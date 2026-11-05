/*
 * AudioOutput 实现：OHAudio renderer + 写回调输出 1 秒 440Hz 正弦波。
 */

#include "audio_output.h"

#include <cmath>
#include <thread>

#include <hilog/log.h>

#undef LOG_DOMAIN
#undef LOG_TAG
#define LOG_DOMAIN 0x0001
#define LOG_TAG "SLH_AudioOutput"

namespace slh {
namespace {

constexpr double kTwoPi = 6.283185307179586476925286766559;
// 幅度取 0.25 满量程，避免削顶，同时足够听清。
constexpr double kToneAmplitude = 0.25 * 32767.0;

} // namespace

int32_t FillToneFrames(int16_t* out, int32_t frameCount, int64_t startFrame)
{
    if (out == nullptr || frameCount <= 0) {
        return 0;
    }
    for (int32_t i = 0; i < frameCount; ++i) {
        const int64_t frame = startFrame + i;
        int16_t value = 0;
        if (frame >= 0 && frame < kToneDurationFrames) {
            const double phase = kTwoPi * kToneFrequencyHz * static_cast<double>(frame) /
                                 static_cast<double>(kToneSampleRate);
            value = static_cast<int16_t>(std::lround(kToneAmplitude * std::sin(phase)));
        }
        for (int32_t ch = 0; ch < kToneChannelCount; ++ch) {
            out[i * kToneChannelCount + ch] = value;
        }
    }
    return frameCount;
}

AudioOutput::~AudioOutput()
{
    StopTone();
}

OH_AudioData_Callback_Result AudioOutput::OnWriteData(OH_AudioRenderer* renderer, void* userData,
                                                      void* audioData, int32_t audioDataSize)
{
    (void)renderer;
    auto* self = static_cast<AudioOutput*>(userData);
    if (self == nullptr || audioData == nullptr || audioDataSize <= 0) {
        return AUDIO_DATA_CALLBACK_RESULT_INVALID;
    }

    // 回调内只做内存写入：按 BlockAlign 计算帧数，余数字节补零。
    const int32_t frameCount = audioDataSize / kToneBlockAlign;
    const int64_t startFrame = self->toneFramePos_.load(std::memory_order_relaxed);
    FillToneFrames(static_cast<int16_t*>(audioData), frameCount, startFrame);
    self->toneFramePos_.store(startFrame + frameCount, std::memory_order_relaxed);

    const int32_t usedBytes = frameCount * kToneBlockAlign;
    if (usedBytes < audioDataSize) {
        auto* tail = static_cast<uint8_t*>(audioData) + usedBytes;
        for (int32_t i = 0; i < audioDataSize - usedBytes; ++i) {
            tail[i] = 0;
        }
    }
    return AUDIO_DATA_CALLBACK_RESULT_VALID;
}

int32_t AudioOutput::EnsureRendererLocked()
{
    if (renderer_ != nullptr) {
        return AUDIOSTREAM_SUCCESS;
    }

    OH_AudioStream_Result ret = OH_AudioStreamBuilder_Create(&builder_, AUDIOSTREAM_TYPE_RENDERER);
    if (ret != AUDIOSTREAM_SUCCESS) {
        OH_LOG_ERROR(LOG_APP, "create builder failed, ret=%{public}d", static_cast<int>(ret));
        builder_ = nullptr;
        return static_cast<int32_t>(ret);
    }

    OH_AudioStreamBuilder_SetSamplingRate(builder_, kToneSampleRate);
    OH_AudioStreamBuilder_SetChannelCount(builder_, kToneChannelCount);
    OH_AudioStreamBuilder_SetSampleFormat(builder_, AUDIOSTREAM_SAMPLE_S16LE);
    OH_AudioStreamBuilder_SetEncodingType(builder_, AUDIOSTREAM_ENCODING_TYPE_RAW);
    OH_AudioStreamBuilder_SetRendererInfo(builder_, AUDIOSTREAM_USAGE_MUSIC);
    OH_AudioStreamBuilder_SetLatencyMode(builder_, AUDIOSTREAM_LATENCY_MODE_NORMAL);
    OH_AudioStreamBuilder_SetRendererWriteDataCallback(builder_, OnWriteData, this);

    ret = OH_AudioStreamBuilder_GenerateRenderer(builder_, &renderer_);
    if (ret != AUDIOSTREAM_SUCCESS) {
        OH_LOG_ERROR(LOG_APP, "generate renderer failed, ret=%{public}d", static_cast<int>(ret));
        renderer_ = nullptr;
        OH_AudioStreamBuilder_Destroy(builder_);
        builder_ = nullptr;
        return static_cast<int32_t>(ret);
    }
    return AUDIOSTREAM_SUCCESS;
}

void AudioOutput::ReleaseRendererLocked()
{
    if (renderer_ != nullptr) {
        OH_AudioRenderer_Stop(renderer_);
        OH_AudioRenderer_Release(renderer_);
        renderer_ = nullptr;
    }
    if (builder_ != nullptr) {
        OH_AudioStreamBuilder_Destroy(builder_);
        builder_ = nullptr;
    }
}

int32_t AudioOutput::StartTone()
{
    std::lock_guard<std::mutex> lock(mutex_);

    const int32_t ret = EnsureRendererLocked();
    if (ret != AUDIOSTREAM_SUCCESS) {
        return ret;
    }

    toneFramePos_.store(0, std::memory_order_relaxed);
    running_.store(true, std::memory_order_relaxed);

    const OH_AudioStream_Result startRet = OH_AudioRenderer_Start(renderer_);
    if (startRet != AUDIOSTREAM_SUCCESS) {
        OH_LOG_ERROR(LOG_APP, "start renderer failed, ret=%{public}d", static_cast<int>(startRet));
        running_.store(false, std::memory_order_relaxed);
        ReleaseRendererLocked();
        return static_cast<int32_t>(startRet);
    }

    OH_LOG_INFO(LOG_APP, "tone started: %{public}d Hz, %{public}d frames",
                static_cast<int>(kToneFrequencyHz), static_cast<int>(kToneDurationFrames));
    return AUDIOSTREAM_SUCCESS;
}

void AudioOutput::StopTone()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_.store(false, std::memory_order_relaxed);
        ReleaseRendererLocked();
    }
    OH_LOG_INFO(LOG_APP, "tone stopped");
}

bool AudioOutput::IsRunning() const
{
    return running_.load(std::memory_order_relaxed);
}

} // namespace slh
