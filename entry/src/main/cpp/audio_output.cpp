/*
 * AudioOutput 实现：OHAudio renderer + 写回调从 RingBuffer 取数。
 */

#include "audio_output.h"

#include <cmath>

#include <hilog/log.h>

#include "audio_output_logic.h"

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
    Stop();
}

void AudioOutput::SetRingBuffer(RingBuffer* ring)
{
    ring_.store(ring, std::memory_order_release);
}

int32_t AudioOutput::Configure(int32_t sampleRate, int32_t channels)
{
    if (sampleRate <= 0 || channels <= 0) {
        return -1;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    // 已建流时格式无法原地修改，先释放，下次 Start 会按新格式重建。
    ReleaseRendererLocked();
    running_.store(false, std::memory_order_release);
    sampleRate_.store(sampleRate, std::memory_order_release);
    channels_.store(channels, std::memory_order_release);
    return 0;
}

int32_t AudioOutput::SampleRate() const
{
    return sampleRate_.load(std::memory_order_acquire);
}

int32_t AudioOutput::Channels() const
{
    return channels_.load(std::memory_order_acquire);
}

int32_t AudioOutput::BlockAlign() const
{
    return Channels() * static_cast<int32_t>(sizeof(int16_t));
}

void AudioOutput::SetUnderrunCallback(UnderrunCallback cb, void* userData)
{
    underrunUserData_.store(userData, std::memory_order_release);
    underrunCb_.store(cb, std::memory_order_release);
}

int64_t AudioOutput::UnderrunBytes() const
{
    return underrunBytes_.load(std::memory_order_acquire);
}

OH_AudioData_Callback_Result AudioOutput::OnWriteData(OH_AudioRenderer* renderer, void* userData,
                                                      void* audioData, int32_t audioDataSize)
{
    (void)renderer;
    auto* self = static_cast<AudioOutput*>(userData);
    if (self == nullptr || audioData == nullptr || audioDataSize <= 0) {
        return AUDIO_DATA_CALLBACK_RESULT_INVALID;
    }

    auto* out = static_cast<uint8_t*>(audioData);

    // G0 冒烟路径：没有 FIFO 时输出正弦波。
    if (self->toneMode_.load(std::memory_order_acquire)) {
        const int32_t frameCount = audioDataSize / kToneBlockAlign;
        const int64_t startFrame = self->toneFramePos_.load(std::memory_order_relaxed);
        FillToneFrames(reinterpret_cast<int16_t*>(audioData), frameCount, startFrame);
        self->toneFramePos_.store(startFrame + frameCount, std::memory_order_relaxed);

        const int32_t usedBytes = frameCount * kToneBlockAlign;
        if (usedBytes < audioDataSize) {
            std::memset(out + usedBytes, 0, static_cast<size_t>(audioDataSize - usedBytes));
        }
        return AUDIO_DATA_CALLBACK_RESULT_VALID;
    }

    RingBuffer* ring = self->ring_.load(std::memory_order_acquire);
    if (ring == nullptr) {
        // 既没有 FIFO 也不在冒烟模式：输出静音，保持流不断。
        std::memset(out, 0, static_cast<size_t>(audioDataSize));
        return AUDIO_DATA_CALLBACK_RESULT_VALID;
    }

    // 回调内只做内存操作：先读 FIFO，不足部分补静音（T17）。
    // BlockAlign 必须取当前渲染器格式，否则立体声文件会被按单声道切分。
    const int32_t blockAlign = self->BlockAlign();
    const size_t available = ring->AvailableToRead();
    const WritePlan plan = PlanWrite(audioDataSize, blockAlign, available);

    int32_t written = 0;
    if (plan.bytesFromFifo > 0) {
        written = static_cast<int32_t>(ring->Read(out, static_cast<size_t>(plan.bytesFromFifo)));
    }
    if (written < audioDataSize) {
        std::memset(out + written, 0, static_cast<size_t>(audioDataSize - written));
    }

    if (IsUnderrun(plan)) {
        self->underrunBytes_.fetch_add(plan.silenceBytes, std::memory_order_acq_rel);
        const UnderrunCallback cb = self->underrunCb_.load(std::memory_order_acquire);
        if (cb != nullptr) {
            cb(self->underrunUserData_.load(std::memory_order_acquire), plan.silenceBytes);
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

    OH_AudioStreamBuilder_SetSamplingRate(builder_, sampleRate_.load(std::memory_order_acquire));
    OH_AudioStreamBuilder_SetChannelCount(builder_, channels_.load(std::memory_order_acquire));
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

int32_t AudioOutput::Start()
{
    std::lock_guard<std::mutex> lock(mutex_);

    const int32_t ret = EnsureRendererLocked();
    if (ret != AUDIOSTREAM_SUCCESS) {
        return ret;
    }

    running_.store(true, std::memory_order_release);
    const OH_AudioStream_Result startRet = OH_AudioRenderer_Start(renderer_);
    if (startRet != AUDIOSTREAM_SUCCESS) {
        OH_LOG_ERROR(LOG_APP, "start renderer failed, ret=%{public}d", static_cast<int>(startRet));
        running_.store(false, std::memory_order_release);
        ReleaseRendererLocked();
        return static_cast<int32_t>(startRet);
    }

    // 数据源按当前模式区分，避免日志把 FIFO 播放误报成正弦波。
    const char* source = toneMode_.load(std::memory_order_acquire) ? "tone" : "ring buffer";
    OH_LOG_INFO(LOG_APP, "renderer started: %{public}d Hz, %{public}d ch, source=%{public}s",
                static_cast<int>(SampleRate()), static_cast<int>(Channels()), source);
    return AUDIOSTREAM_SUCCESS;
}

int32_t AudioOutput::StartTone()
{
    toneFramePos_.store(0, std::memory_order_release);
    toneMode_.store(true, std::memory_order_release);
    const int32_t ret = Start();
    if (ret != AUDIOSTREAM_SUCCESS) {
        toneMode_.store(false, std::memory_order_release);
    }
    return ret;
}

void AudioOutput::Stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_.store(false, std::memory_order_release);
        ReleaseRendererLocked();
    }
    toneMode_.store(false, std::memory_order_release);
    OH_LOG_INFO(LOG_APP, "renderer stopped, total underrun bytes=%{public}lld",
                static_cast<long long>(UnderrunBytes()));
}

bool AudioOutput::IsRunning() const
{
    return running_.load(std::memory_order_acquire);
}

} // namespace slh
