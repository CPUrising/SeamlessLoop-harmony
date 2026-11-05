# 最小版本要求（与 DevEco Studio 模板一致）
cmake_minimum_required(VERSION 3.5.0)
project(seamless_loop_harmony)

set(NATIVERENDER_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR})

if(DEFINED PACKAGE_FIND_FILE)
    include(${PACKAGE_FIND_FILE})
endif()

include_directories(${NATIVERENDER_ROOT_PATH}
                    ${NATIVERENDER_ROOT_PATH}/include)

# ---------------------------------------------------------------------------
# 可移植的纯逻辑部分：不依赖 OHAudio / napi / hilog，开发机上可直接单测。
# ---------------------------------------------------------------------------
set(SLH_CORE_SOURCES
    audio_decoder.cpp
    ring_buffer.cpp
    third_party/dr_libs_impl.c)

# ---------------------------------------------------------------------------
# 两条构建路径：
#   1) hvigor 构建 HAP（默认）：产出 libentry.so，链接 OHAudio / napi / hilog。
#   2) 开发机单测（-DBUILD_TESTS=ON 且非 OHOS 工具链）：只编纯逻辑 + 测试。
# 之所以分开，是因为 libohaudio.so / libace_napi.z.so 只存在于 OHOS sysroot，
# 本机 MSVC 链接不到。
# ---------------------------------------------------------------------------
option(BUILD_TESTS "Build host-side unit tests" OFF)

if(BUILD_TESTS AND NOT CMAKE_SYSTEM_NAME STREQUAL "OHOS")
    enable_testing()
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    add_executable(ring_buffer_tests
        tests/ring_buffer_test.cpp
        ring_buffer.cpp)
    target_include_directories(ring_buffer_tests PRIVATE
        ${NATIVERENDER_ROOT_PATH}
        ${NATIVERENDER_ROOT_PATH}/tests)
    add_test(NAME RingBufferTests COMMAND ring_buffer_tests)

    add_executable(audio_decoder_tests
        tests/audio_decoder_test.cpp
        ${SLH_CORE_SOURCES})
    target_include_directories(audio_decoder_tests PRIVATE
        ${NATIVERENDER_ROOT_PATH}
        ${NATIVERENDER_ROOT_PATH}/tests)
    # 合成 fixture 目录：cpp → main → src → entry → 工程根
    target_compile_definitions(audio_decoder_tests PRIVATE
        FIXTURE_DIR="${NATIVERENDER_ROOT_PATH}/../../../../fixtures/synthetic")
    add_test(NAME DecoderTests COMMAND audio_decoder_tests)

    return()
endif()

# ---------------------------------------------------------------------------
# HAP 构建路径
# ---------------------------------------------------------------------------
# libentry.so：napi 桥接 + OHAudio 输出
add_library(entry SHARED
    napi_init.cpp
    audio_output.cpp
    ${SLH_CORE_SOURCES})

# libace_napi.z.so 提供 napi 运行时；libohaudio.so 提供 OHAudio C API；
# libhilog_ndk.z.so 提供 OH_LOG_* 日志宏。
target_link_libraries(entry PUBLIC
    libace_napi.z.so
    libohaudio.so
    libhilog_ndk.z.so)
