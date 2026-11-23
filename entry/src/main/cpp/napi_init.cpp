/*
 * napi 桥接层：极薄，只做类型转换与调用转发，不含业务逻辑（design.md 决策 7）。
 *
 * 暴露的 API：
 *   load(path) / play() / pause() / stop() / seekToSample(frame)
 *   setLoopPoints(start, end) / setLooping(enabled)
 *   getProgress() / getState() / getFormat() / getLoopCount()
 *   on(event, callback) / off(event)
 *
 * 事件（ThreadSafeFunction 投递到 JS 线程）：
 *   loop     —— 每次循环跳转，参数为累计次数
 *   end      —— 播放到末尾
 *   underrun —— 缓冲欠载，参数为本次静音字节数（T17 第 3 条）
 *
 * 线程模型：
 * - 解码线程与 OHAudio 回调线程都会触发事件，napi 不允许在这些线程直接调 JS，
 *   因此统一经 ThreadSafeFunction 投递。
 * - 回调对象在 on() 时创建、off() 或模块卸载时释放；投递前检查是否仍有效。
 */

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

#include "napi/native_api.h"

#include "audio_output.h"
#include "playback_engine.h"

namespace {

slh::PlaybackEngine g_engine;
slh::AudioOutput g_audioOutput;

// 事件名。
constexpr const char* kEventLoop = "loop";
constexpr const char* kEventEnd = "end";
constexpr const char* kEventUnderrun = "underrun";

// 每个事件一个 ThreadSafeFunction 句柄。
struct EventSlot {
    napi_threadsafe_function tsfn = nullptr;
    std::mutex mutex;
};

EventSlot g_loopSlot;
EventSlot g_endSlot;
EventSlot g_underrunSlot;

// 投递到 JS 线程的数据。由 JS 线程释放。
struct EventPayload {
    int64_t value = 0;
};

void CallJs(napi_env env, napi_value jsCallback, void* /*context*/, void* data)
{
    auto* payload = static_cast<EventPayload*>(data);
    if (env != nullptr && jsCallback != nullptr && payload != nullptr) {
        napi_value arg = nullptr;
        napi_create_int64(env, payload->value, &arg);
        napi_value global = nullptr;
        napi_get_global(env, &global);
        napi_call_function(env, global, jsCallback, 1, &arg, nullptr);
    }
    delete payload;
}

void PostEvent(EventSlot& slot, int64_t value)
{
    std::lock_guard<std::mutex> lock(slot.mutex);
    if (slot.tsfn == nullptr) {
        return;
    }
    auto* payload = new EventPayload{value};
    // 队列满时丢弃事件而不是阻塞音频线程；欠载/循环事件是通知性质，丢一次不影响正确性。
    if (napi_call_threadsafe_function(slot.tsfn, payload, napi_tsfn_nonblocking) != napi_ok) {
        delete payload;
    }
}

// 以下三个回调运行在解码线程 / OHAudio 回调线程上，必须极快返回。
void OnLoopEvent(void* /*userData*/, int64_t loopCount)
{
    PostEvent(g_loopSlot, loopCount);
}

void OnEndEvent(void* /*userData*/)
{
    PostEvent(g_endSlot, 0);
}

void OnUnderrunEvent(void* /*userData*/, int64_t underrunBytes)
{
    PostEvent(g_underrunSlot, underrunBytes);
}

// ---------- 参数与返回值辅助 ----------

bool GetInt64Arg(napi_env env, napi_value value, int64_t* out)
{
    napi_valuetype type = napi_undefined;
    if (napi_typeof(env, value, &type) != napi_ok || type != napi_number) {
        return false;
    }
    return napi_get_value_int64(env, value, out) == napi_ok;
}

napi_value MakeInt64(napi_env env, int64_t value)
{
    napi_value result = nullptr;
    napi_create_int64(env, value, &result);
    return result;
}

napi_value MakeBool(napi_env env, bool value)
{
    napi_value result = nullptr;
    napi_get_boolean(env, value, &result);
    return result;
}

napi_value MakeUndefined(napi_env env)
{
    napi_value result = nullptr;
    napi_get_undefined(env, &result);
    return result;
}

// 把渲染器与引擎接起来：格式、数据源、欠载回调。
void AttachEngineToOutput()
{
    const slh::AudioFormatInfo& info = g_engine.Format();
    g_audioOutput.Configure(info.sampleRate, info.channels);
    g_audioOutput.SetRingBuffer(g_engine.Ring());
    g_audioOutput.SetUnderrunCallback(OnUnderrunEvent, nullptr);
}

// ---------- API 实现 ----------

napi_value Load(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 1) {
        return MakeBool(env, false);
    }
    size_t len = 0;
    if (napi_get_value_string_utf8(env, args[0], nullptr, 0, &len) != napi_ok) {
        return MakeBool(env, false);
    }
    std::string path(len, '\0');
    if (napi_get_value_string_utf8(env, args[0], &path[0], len + 1, &len) != napi_ok) {
        return MakeBool(env, false);
    }

    // 换文件前先停掉渲染器，避免回调读到已释放的 FIFO。
    g_audioOutput.Stop();
    if (!g_engine.Load(path.c_str())) {
        return MakeBool(env, false);
    }
    AttachEngineToOutput();
    g_engine.SetLoopCallback(OnLoopEvent, nullptr);
    g_engine.SetEndCallback(OnEndEvent, nullptr);
    return MakeBool(env, true);
}

napi_value Play(napi_env env, napi_callback_info info)
{
    (void)info;
    if (!g_engine.Play()) {
        return MakeBool(env, false);
    }
    // 渲染器先起，解码线程随后供数；顺序反了会先欠载一小段。
    const int32_t ret = g_audioOutput.Start();
    if (ret != 0) {
        g_engine.Pause();
        return MakeBool(env, false);
    }
    return MakeBool(env, true);
}

napi_value Pause(napi_env env, napi_callback_info info)
{
    (void)info;
    g_engine.Pause();
    // 暂停时停渲染器，释放音频焦点；FIFO 与位置保留，恢复时继续。
    g_audioOutput.Stop();
    return MakeUndefined(env);
}

napi_value Stop(napi_env env, napi_callback_info info)
{
    (void)info;
    g_audioOutput.Stop();
    g_engine.Stop();
    return MakeUndefined(env);
}

napi_value SeekToSample(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    int64_t frame = 0;
    if (argc < 1 || !GetInt64Arg(env, args[0], &frame)) {
        return MakeBool(env, false);
    }
    return MakeBool(env, g_engine.SeekToSample(frame));
}

napi_value SetLoopPoints(napi_env env, napi_callback_info info)
{
    size_t argc = 2;
    napi_value args[2] = {nullptr, nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    int64_t start = 0;
    int64_t end = 0;
    if (argc < 2 || !GetInt64Arg(env, args[0], &start) || !GetInt64Arg(env, args[1], &end)) {
        return MakeBool(env, false);
    }
    return MakeBool(env, g_engine.SetLoopRange(start, end));
}

napi_value SetLooping(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    bool enabled = false;
    if (argc < 1 || napi_get_value_bool(env, args[0], &enabled) != napi_ok) {
        return MakeUndefined(env);
    }
    g_engine.SetLooping(enabled);
    return MakeUndefined(env);
}

napi_value GetProgress(napi_env env, napi_callback_info info)
{
    (void)info;
    return MakeInt64(env, g_engine.PlaybackFrame());
}

napi_value GetLoopCount(napi_env env, napi_callback_info info)
{
    (void)info;
    return MakeInt64(env, g_engine.LoopCount());
}

napi_value GetState(napi_env env, napi_callback_info info)
{
    (void)info;
    // 0=idle 1=ready 2=playing 3=paused，与 ArkTS 侧常量一致。
    return MakeInt64(env, static_cast<int64_t>(g_engine.State()));
}

napi_value GetFormat(napi_env env, napi_callback_info info)
{
    (void)info;
    const slh::AudioFormatInfo& info2 = g_engine.Format();
    napi_value obj = nullptr;
    napi_create_object(env, &obj);

    napi_value sampleRate = nullptr;
    napi_create_int32(env, info2.sampleRate, &sampleRate);
    napi_set_named_property(env, obj, "sampleRate", sampleRate);

    napi_value channels = nullptr;
    napi_create_int32(env, info2.channels, &channels);
    napi_set_named_property(env, obj, "channels", channels);

    napi_value totalFrames = nullptr;
    napi_create_int64(env, info2.totalFrames, &totalFrames);
    napi_set_named_property(env, obj, "totalFrames", totalFrames);

    // 是否按 A/B 双文件（虚拟时间轴）加载，供界面显示。
    napi_value isPair = nullptr;
    napi_get_boolean(env, g_engine.IsPair(), &isPair);
    napi_set_named_property(env, obj, "isPair", isPair);

    return obj;
}

// ---------- 事件订阅 ----------

EventSlot* SlotForName(const std::string& name)
{
    if (name == kEventLoop) {
        return &g_loopSlot;
    }
    if (name == kEventEnd) {
        return &g_endSlot;
    }
    if (name == kEventUnderrun) {
        return &g_underrunSlot;
    }
    return nullptr;
}

napi_value On(napi_env env, napi_callback_info info)
{
    size_t argc = 2;
    napi_value args[2] = {nullptr, nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 2) {
        return MakeBool(env, false);
    }

    size_t len = 0;
    if (napi_get_value_string_utf8(env, args[0], nullptr, 0, &len) != napi_ok) {
        return MakeBool(env, false);
    }
    std::string name(len, '\0');
    napi_get_value_string_utf8(env, args[0], &name[0], len + 1, &len);

    EventSlot* slot = SlotForName(name);
    if (slot == nullptr) {
        return MakeBool(env, false);
    }

    napi_valuetype type = napi_undefined;
    if (napi_typeof(env, args[1], &type) != napi_ok || type != napi_function) {
        return MakeBool(env, false);
    }

    napi_value resourceName = nullptr;
    napi_create_string_utf8(env, name.c_str(), NAPI_AUTO_LENGTH, &resourceName);

    napi_threadsafe_function tsfn = nullptr;
    if (napi_create_threadsafe_function(env, args[1], nullptr, resourceName, 0, 1, nullptr, nullptr,
                                        nullptr, CallJs, &tsfn) != napi_ok) {
        return MakeBool(env, false);
    }

    std::lock_guard<std::mutex> lock(slot->mutex);
    if (slot->tsfn != nullptr) {
        // 重复订阅：释放旧句柄，避免泄漏。
        napi_release_threadsafe_function(slot->tsfn, napi_tsfn_abort);
    }
    slot->tsfn = tsfn;
    return MakeBool(env, true);
}

napi_value Off(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 1) {
        return MakeUndefined(env);
    }

    size_t len = 0;
    if (napi_get_value_string_utf8(env, args[0], nullptr, 0, &len) != napi_ok) {
        return MakeUndefined(env);
    }
    std::string name(len, '\0');
    napi_get_value_string_utf8(env, args[0], &name[0], len + 1, &len);

    EventSlot* slot = SlotForName(name);
    if (slot == nullptr) {
        return MakeUndefined(env);
    }

    std::lock_guard<std::mutex> lock(slot->mutex);
    if (slot->tsfn != nullptr) {
        napi_release_threadsafe_function(slot->tsfn, napi_tsfn_abort);
        slot->tsfn = nullptr;
    }
    return MakeUndefined(env);
}

// ---------- G0 冒烟兼容入口 ----------

napi_value StartTone(napi_env env, napi_callback_info info)
{
    (void)info;
    const int32_t ret = g_audioOutput.StartTone();
    napi_value result = nullptr;
    napi_create_int32(env, ret, &result);
    return result;
}

napi_value StopTone(napi_env env, napi_callback_info info)
{
    (void)info;
    g_audioOutput.StopTone();
    return MakeUndefined(env);
}

napi_value IsToneRunning(napi_env env, napi_callback_info info)
{
    (void)info;
    return MakeBool(env, g_audioOutput.IsRunning());
}

} // namespace

EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports)
{
    napi_property_descriptor desc[] = {
        // 播放控制
        { "load", nullptr, Load, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "play", nullptr, Play, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "pause", nullptr, Pause, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "stop", nullptr, Stop, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "seekToSample", nullptr, SeekToSample, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "setLoopPoints", nullptr, SetLoopPoints, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "setLooping", nullptr, SetLooping, nullptr, nullptr, nullptr, napi_default, nullptr },
        // 状态查询
        { "getProgress", nullptr, GetProgress, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "getLoopCount", nullptr, GetLoopCount, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "getState", nullptr, GetState, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "getFormat", nullptr, GetFormat, nullptr, nullptr, nullptr, napi_default, nullptr },
        // 事件
        { "on", nullptr, On, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "off", nullptr, Off, nullptr, nullptr, nullptr, napi_default, nullptr },
        // G0 冒烟兼容
        { "startTone", nullptr, StartTone, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "stopTone", nullptr, StopTone, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "isToneRunning", nullptr, IsToneRunning, nullptr, nullptr, nullptr, napi_default, nullptr },
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
    return exports;
}
EXTERN_C_END

static napi_module entryModule = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = Init,
    .nm_modname = "entry",
    .nm_priv = ((void*)0),
    .reserved = { 0 },
};

extern "C" __attribute__((constructor)) void RegisterEntryModule(void)
{
    napi_module_register(&entryModule);
}
