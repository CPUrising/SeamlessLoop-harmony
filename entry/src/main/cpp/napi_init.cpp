/*
 * napi 桥接层：极薄，只做类型转换与调用转发，不含业务逻辑。
 *
 * 本阶段（任务 2.4）只暴露 startTone() / stopTone()，用于 G0 冒烟。
 */

#include <cstdint>

#include "napi/native_api.h"

#include "audio_output.h"

namespace {

slh::AudioOutput g_audioOutput;

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
    napi_value result = nullptr;
    napi_get_undefined(env, &result);
    return result;
}

napi_value IsToneRunning(napi_env env, napi_callback_info info)
{
    (void)info;
    napi_value result = nullptr;
    napi_get_boolean(env, g_audioOutput.IsRunning(), &result);
    return result;
}

} // namespace

EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports)
{
    napi_property_descriptor desc[] = {
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
