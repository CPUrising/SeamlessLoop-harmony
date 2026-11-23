/*
 * PlaybackEngine：播放控制门面（对应任务 5.2，覆盖 T13 / T14）。
 *
 * 职责：
 * - 持有 AudioSource / RingBuffer / DecodeThread 三件套，对外只暴露
 *   load / play / pause / stop / seekToSample / 进度查询。
 * - Load 时自动识别 A/B 配对：命中则按虚拟时间轴加载，否则按单文件加载。
 * - 不依赖 OHAudio：渲染器的启停由桥接层负责，本类只负责「供数」。
 *   这样播放控制逻辑可以在开发机上完整单测。
 *
 * 状态机：
 *   Idle --Load--> Ready --Play--> Playing --Pause--> Paused --Play--> Playing
 *                              \--Stop--> Ready
 *
 * 暂停语义（T13）：
 *   暂停只停解码线程，不清空 FIFO、不移动解码位置。恢复时解码线程从
 *   解码器当前位置继续，FIFO 里已有的数据也照常播出，因此听感位置连续。
 *
 * seek 语义（T14）：
 *   请求值 clamp 到 [0, totalFrames]，按帧对齐（一帧 = BlockAlign），
 *   清空 FIFO 避免播出跳转前的残留音频，并重置循环计数（即补偿清零）。
 */

#ifndef SEAMLESS_LOOP_PLAYBACK_ENGINE_H
#define SEAMLESS_LOOP_PLAYBACK_ENGINE_H

#include <cstdint>
#include <memory>
#include <string>

#include "audio_source.h"
#include "decode_thread.h"
#include "ring_buffer.h"

namespace slh {

enum class PlaybackState {
    kIdle = 0, // 未加载
    kReady,    // 已加载，未播放
    kPlaying,
    kPaused,
};

class PlaybackEngine {
public:
    PlaybackEngine();
    ~PlaybackEngine();

    PlaybackEngine(const PlaybackEngine&) = delete;
    PlaybackEngine& operator=(const PlaybackEngine&) = delete;

    // 加载文件并准备 2 秒 FIFO。重复调用会先释放上一个文件。
    // 若 path 属于 A/B 对（同目录存在配对文件），自动按虚拟时间轴加载。
    // 失败返回 false，状态回到 kIdle。
    bool Load(const char* path);

    // 释放文件与线程，回到 kIdle。
    void Unload();

    // 开始或恢复播放。未加载时返回 false。
    bool Play();

    // 暂停：停解码线程，保留 FIFO 与解码位置（T13）。
    void Pause();

    // 停止：停线程、清空 FIFO、回到文件开头、循环计数归零。
    void Stop();

    // 按采样点跳转（T14）。未加载时返回 false。
    bool SeekToSample(int64_t frame);

    PlaybackState State() const { return state_; }

    bool IsPlaying() const { return state_ == PlaybackState::kPlaying; }

    // 已补偿的播放位置（帧）。
    int64_t PlaybackFrame() const;

    // 当前解码位置（帧），未加载时为 0。
    int64_t DecodeFrame() const;

    int64_t TotalFrames() const;

    // 供桥接层与测试访问内部组件；生命周期由本类管理。
    RingBuffer* Ring() { return ring_.get(); }
    DecodeThread* Decoder() { return decodeThread_.get(); }
    AudioSource* Source() { return source_.get(); }
    const AudioFormatInfo& Format() const;

    // 当前加载的是否为 A/B 双文件（虚拟时间轴）。
    bool IsPair() const { return source_ != nullptr && source_->IsPair(); }

    // 循环区间与开关，直接转发给解码线程。
    bool SetLoopRange(int64_t startFrame, int64_t endFrame);
    void SetLooping(bool enabled);
    bool IsLooping() const;
    int64_t LoopCount() const;

    // 事件回调转发。
    void SetLoopCallback(DecodeThread::LoopCallback callback, void* userData);
    void SetEndCallback(DecodeThread::EndCallback callback, void* userData);

private:
    void ReleaseAll();

    // 清空 FIFO 中的残留音频。
    // 消费者（OHAudio 回调）在跑时必须走 RequestFlush 让它自己应用，
    // 否则会与 Read() 竞争写 readCounter_，破坏 SPSC 前提；
    // 只有在确认没有消费者（等待超时）时才直接 Clear()。
    void FlushBuffer();

    std::unique_ptr<AudioSource> source_;
    std::unique_ptr<RingBuffer> ring_;
    std::unique_ptr<DecodeThread> decodeThread_;
    PlaybackState state_ = PlaybackState::kIdle;
};

} // namespace slh

#endif // SEAMLESS_LOOP_PLAYBACK_ENGINE_H
