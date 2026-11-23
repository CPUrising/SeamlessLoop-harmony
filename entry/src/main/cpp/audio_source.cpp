/*
 * AudioSource 实现：把 1 或 2 个 AudioDecoder 串成一条虚拟时间轴。
 */

#include "audio_source.h"

namespace slh {

AudioSource::AudioSource() = default;

AudioSource::~AudioSource()
{
    Close();
}

bool AudioSource::Open(const char* path)
{
    Close();
    if (path == nullptr) {
        return false;
    }

    auto decoder = std::make_unique<AudioDecoder>();
    if (!decoder->Open(path)) {
        return false;
    }

    aDecoder_ = std::move(decoder);
    info_ = aDecoder_->Format();
    aFrames_ = info_.totalFrames;
    isPair_ = false;
    activeIndex_ = 0;
    currentFrame_ = 0;
    return true;
}

bool AudioSource::OpenPair(const char* aPath, const char* bPath)
{
    Close();
    if (aPath == nullptr || bPath == nullptr) {
        return false;
    }

    auto a = std::make_unique<AudioDecoder>();
    if (!a->Open(aPath)) {
        return false;
    }
    auto b = std::make_unique<AudioDecoder>();
    if (!b->Open(bPath)) {
        return false;
    }

    // 采样率与声道数必须一致，否则拼接后会出现错位音频。
    const AudioFormatInfo& aInfo = a->Format();
    const AudioFormatInfo& bInfo = b->Format();
    if (aInfo.sampleRate != bInfo.sampleRate || aInfo.channels != bInfo.channels) {
        return false;
    }

    aDecoder_ = std::move(a);
    bDecoder_ = std::move(b);
    info_ = aDecoder_->Format();
    aFrames_ = info_.totalFrames;
    // 虚拟轴总长 = A + B；采样率与声道数取 A 的值（P04）。
    info_.totalFrames = aFrames_ + bDecoder_->Format().totalFrames;
    isPair_ = true;
    activeIndex_ = 0;
    currentFrame_ = 0;
    return true;
}

void AudioSource::Close()
{
    if (aDecoder_ != nullptr) {
        aDecoder_->Close();
    }
    if (bDecoder_ != nullptr) {
        bDecoder_->Close();
    }
    aDecoder_.reset();
    bDecoder_.reset();
    info_ = AudioFormatInfo{};
    aFrames_ = 0;
    isPair_ = false;
    activeIndex_ = 0;
    currentFrame_ = 0;
}

bool AudioSource::IsOpen() const
{
    return aDecoder_ != nullptr && aDecoder_->IsOpen();
}

bool AudioSource::IsPair() const
{
    return isPair_;
}

const AudioFormatInfo& AudioSource::Format() const
{
    return info_;
}

AudioDecoder* AudioSource::ActiveDecoder() const
{
    if (activeIndex_ == 1 && bDecoder_ != nullptr) {
        return bDecoder_.get();
    }
    return aDecoder_.get();
}

bool AudioSource::SwitchToNextFile()
{
    // 只有 A/B 模式且当前在 A 时才需要切换。
    if (!isPair_ || activeIndex_ != 0 || bDecoder_ == nullptr) {
        return false;
    }
    // 每次进入 B 都从 B 的第 0 帧开始：循环回 A 段后再次跨越时，
    // B 的解码器可能还停在上一次的末尾。
    if (bDecoder_->SeekToFrame(0) < 0) {
        return false;
    }
    activeIndex_ = 1;
    return true;
}

int64_t AudioSource::SeekToFrame(int64_t frame)
{
    if (!IsOpen()) {
        return -1;
    }

    // clamp 到 [0, totalFrames]。
    int64_t target = frame;
    if (target < 0) {
        target = 0;
    }
    if (target > info_.totalFrames) {
        target = info_.totalFrames;
    }

    // P07：按虚拟轴位置决定落在哪个文件。
    if (target < aFrames_) {
        activeIndex_ = 0;
        const int64_t landed = aDecoder_->SeekToFrame(target);
        if (landed < 0) {
            return -1;
        }
        currentFrame_ = landed;
        return landed;
    }

    if (bDecoder_ == nullptr) {
        // 单文件模式：target 已被 clamp 到 totalFrames，落在 A 末尾。
        activeIndex_ = 0;
        const int64_t landed = aDecoder_->SeekToFrame(target);
        if (landed < 0) {
            return -1;
        }
        currentFrame_ = landed;
        return landed;
    }

    activeIndex_ = 1;
    const int64_t landed = bDecoder_->SeekToFrame(target - aFrames_);
    if (landed < 0) {
        return -1;
    }
    currentFrame_ = aFrames_ + landed;
    return currentFrame_;
}

int64_t AudioSource::ReadFrames(int16_t* out, int64_t frames)
{
    if (!IsOpen() || out == nullptr || frames <= 0) {
        return 0;
    }

    const int32_t channels = info_.channels;
    int64_t total = 0;

    while (total < frames) {
        AudioDecoder* decoder = ActiveDecoder();
        if (decoder == nullptr) {
            break;
        }

        const int64_t got = decoder->ReadFrames(out + total * channels, frames - total);
        if (got > 0) {
            total += got;
            continue;
        }

        // 当前文件读完：A/B 模式下切到 B 继续读（P05），不返回 0。
        if (!SwitchToNextFile()) {
            break;
        }
    }

    // 更新虚拟轴位置。
    if (activeIndex_ == 1 && bDecoder_ != nullptr) {
        currentFrame_ = aFrames_ + bDecoder_->CurrentFrame();
    } else if (aDecoder_ != nullptr) {
        currentFrame_ = aDecoder_->CurrentFrame();
    }
    return total;
}

int64_t AudioSource::CurrentFrame() const
{
    return currentFrame_;
}

} // namespace slh
