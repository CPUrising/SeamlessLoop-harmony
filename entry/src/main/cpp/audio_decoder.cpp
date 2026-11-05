/*
 * AudioDecoder 实现：dr_wav / dr_mp3 的统一封装。
 *
 * 关键约定见 audio_decoder.h 顶部注释。
 */

#include "audio_decoder.h"

#include <cstring>
#include <string>

#include "third_party/dr_mp3.h"
#include "third_party/dr_wav.h"

namespace slh {
namespace {

bool HasExtension(const char* path, const char* ext)
{
    if (path == nullptr || ext == nullptr) {
        return false;
    }
    const size_t pathLen = std::strlen(path);
    const size_t extLen = std::strlen(ext);
    if (pathLen < extLen) {
        return false;
    }
    const char* tail = path + (pathLen - extLen);
    for (size_t i = 0; i < extLen; ++i) {
        char c = tail[i];
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
        if (c != ext[i]) {
            return false;
        }
    }
    return true;
}

} // namespace

struct AudioDecoder::Impl {
    AudioFormatInfo info;

    // 两种解码器互斥使用，同一时刻只有一个是打开的。
    drwav wav{};
    drmp3 mp3{};
    bool wavOpen = false;
    bool mp3Open = false;

    // 逻辑解码位置（帧）。不用 dr_libs 的内部游标，避免两套语义混淆。
    int64_t currentFrame = 0;

    void CloseAll()
    {
        if (wavOpen) {
            drwav_uninit(&wav);
            wavOpen = false;
        }
        if (mp3Open) {
            drmp3_uninit(&mp3);
            mp3Open = false;
        }
        info = AudioFormatInfo{};
        currentFrame = 0;
    }
};

AudioDecoder::AudioDecoder() : impl_(new Impl()) {}

AudioDecoder::~AudioDecoder()
{
    impl_->CloseAll();
}

bool AudioDecoder::Open(const char* path)
{
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    impl_->CloseAll();

    if (HasExtension(path, ".wav")) {
        if (!drwav_init_file(&impl_->wav, path, nullptr)) {
            return false;
        }
        impl_->wavOpen = true;
        impl_->info.format = AudioFormat::kWav;
        impl_->info.sampleRate = static_cast<int32_t>(impl_->wav.sampleRate);
        impl_->info.channels = static_cast<int32_t>(impl_->wav.channels);
        impl_->info.totalFrames = static_cast<int64_t>(impl_->wav.totalPCMFrameCount);
        impl_->currentFrame = 0;
        return true;
    }

    if (HasExtension(path, ".mp3")) {
        if (!drmp3_init_file(&impl_->mp3, path, nullptr)) {
            return false;
        }
        impl_->mp3Open = true;
        impl_->info.format = AudioFormat::kMp3;
        impl_->info.sampleRate = static_cast<int32_t>(impl_->mp3.sampleRate);
        impl_->info.channels = static_cast<int32_t>(impl_->mp3.channels);
        // drmp3_get_pcm_frame_count 已扣除编码器延迟与尾部填充，
        // 因此这个值与实际可读出的帧数一致（见 dr_mp3.h:4569-4600）。
        impl_->info.totalFrames = static_cast<int64_t>(drmp3_get_pcm_frame_count(&impl_->mp3));
        impl_->currentFrame = 0;
        return true;
    }

    // 扩展名不支持：明确失败，不猜测格式。
    return false;
}

void AudioDecoder::Close()
{
    impl_->CloseAll();
}

bool AudioDecoder::IsOpen() const
{
    return impl_->wavOpen || impl_->mp3Open;
}

const AudioFormatInfo& AudioDecoder::Format() const
{
    return impl_->info;
}

int64_t AudioDecoder::SeekToFrame(int64_t frame)
{
    if (!IsOpen()) {
        return -1;
    }

    // clamp 到 [0, totalFrames]，对应 T14 的 clamp 要求。
    int64_t target = frame;
    if (target < 0) {
        target = 0;
    }
    if (target > impl_->info.totalFrames) {
        target = impl_->info.totalFrames;
    }

    // 按帧 seek 天然满足 BlockAlign 对齐：一帧 = channels * 2 字节。
    if (impl_->wavOpen) {
        if (!drwav_seek_to_pcm_frame(&impl_->wav, static_cast<drwav_uint64>(target))) {
            return -1;
        }
    } else if (impl_->mp3Open) {
        if (!drmp3_seek_to_pcm_frame(&impl_->mp3, static_cast<drmp3_uint64>(target))) {
            return -1;
        }
    } else {
        return -1;
    }

    impl_->currentFrame = target;
    return target;
}

int64_t AudioDecoder::ReadFrames(int16_t* out, int64_t frames)
{
    if (!IsOpen() || out == nullptr || frames <= 0) {
        return 0;
    }

    int64_t read = 0;
    if (impl_->wavOpen) {
        read = static_cast<int64_t>(
            drwav_read_pcm_frames_s16(&impl_->wav, static_cast<drwav_uint64>(frames), out));
    } else if (impl_->mp3Open) {
        read = static_cast<int64_t>(
            drmp3_read_pcm_frames_s16(&impl_->mp3, static_cast<drmp3_uint64>(frames), out));
    }

    if (read > 0) {
        impl_->currentFrame += read;
    }
    return read;
}

int64_t AudioDecoder::CurrentFrame() const
{
    return impl_->currentFrame;
}

} // namespace slh
