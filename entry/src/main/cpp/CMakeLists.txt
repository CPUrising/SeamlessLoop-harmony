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
    decode_thread.cpp
    loop_config.cpp
    playback_engine.cpp
    ab_pair.cpp
    audio_source.cpp
    third_party/dr_libs_impl.c
    third_party/stb_vorbis_impl.c)

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

    # 源码是 UTF-8（含中文注释）。MSVC 默认按系统代码页（936）解析，
    # 会把注释里的多字节字符吞进下一行导致语法错误，必须显式指定 /utf-8。
    if(MSVC)
        add_compile_options(/utf-8)
    endif()

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

    add_executable(audio_output_logic_tests
        tests/audio_output_logic_test.cpp)
    target_include_directories(audio_output_logic_tests PRIVATE
        ${NATIVERENDER_ROOT_PATH}
        ${NATIVERENDER_ROOT_PATH}/tests)
    add_test(NAME AudioOutputLogicTests COMMAND audio_output_logic_tests)

    add_executable(decode_thread_tests
        tests/decode_thread_test.cpp
        ${SLH_CORE_SOURCES})
    target_include_directories(decode_thread_tests PRIVATE
        ${NATIVERENDER_ROOT_PATH}
        ${NATIVERENDER_ROOT_PATH}/tests)
    target_compile_definitions(decode_thread_tests PRIVATE
        FIXTURE_DIR="${NATIVERENDER_ROOT_PATH}/../../../../fixtures/synthetic")
    add_test(NAME DecodeThreadTests COMMAND decode_thread_tests)

    add_executable(loop_config_tests
        tests/loop_config_test.cpp
        loop_config.cpp)
    target_include_directories(loop_config_tests PRIVATE
        ${NATIVERENDER_ROOT_PATH}
        ${NATIVERENDER_ROOT_PATH}/tests)
    add_test(NAME LoopConfigTests COMMAND loop_config_tests)

    add_executable(ab_pair_tests
        tests/ab_pair_test.cpp
        ab_pair.cpp)
    target_include_directories(ab_pair_tests PRIVATE
        ${NATIVERENDER_ROOT_PATH}
        ${NATIVERENDER_ROOT_PATH}/tests)
    target_compile_definitions(ab_pair_tests PRIVATE
        FIXTURE_DIR="${NATIVERENDER_ROOT_PATH}/../../../../fixtures/synthetic")
    add_test(NAME AbPairTests COMMAND ab_pair_tests)

    add_executable(audio_source_tests
        tests/audio_source_test.cpp
        ${SLH_CORE_SOURCES})
    target_include_directories(audio_source_tests PRIVATE
        ${NATIVERENDER_ROOT_PATH}
        ${NATIVERENDER_ROOT_PATH}/tests)
    target_compile_definitions(audio_source_tests PRIVATE
        FIXTURE_DIR="${NATIVERENDER_ROOT_PATH}/../../../../fixtures/synthetic")
    add_test(NAME AudioSourceTests COMMAND audio_source_tests)

    add_executable(loop_counter_tests
        tests/loop_counter_test.cpp
        ${SLH_CORE_SOURCES})
    target_include_directories(loop_counter_tests PRIVATE
        ${NATIVERENDER_ROOT_PATH}
        ${NATIVERENDER_ROOT_PATH}/tests)
    target_compile_definitions(loop_counter_tests PRIVATE
        FIXTURE_DIR="${NATIVERENDER_ROOT_PATH}/../../../../fixtures/synthetic")
    add_test(NAME LoopCounterTests COMMAND loop_counter_tests)

    add_executable(loop_range_change_tests
        tests/loop_range_change_test.cpp
        ${SLH_CORE_SOURCES})
    target_include_directories(loop_range_change_tests PRIVATE
        ${NATIVERENDER_ROOT_PATH}
        ${NATIVERENDER_ROOT_PATH}/tests)
    target_compile_definitions(loop_range_change_tests PRIVATE
        FIXTURE_DIR="${NATIVERENDER_ROOT_PATH}/../../../../fixtures/synthetic")
    add_test(NAME LoopRangeChangeTests COMMAND loop_range_change_tests)

    add_executable(loop_stream_tests
        tests/loop_stream_test.cpp
        ${SLH_CORE_SOURCES})
    target_include_directories(loop_stream_tests PRIVATE
        ${NATIVERENDER_ROOT_PATH}
        ${NATIVERENDER_ROOT_PATH}/tests)
    target_compile_definitions(loop_stream_tests PRIVATE
        FIXTURE_DIR="${NATIVERENDER_ROOT_PATH}/../../../../fixtures/synthetic")
    add_test(NAME LoopStreamTests COMMAND loop_stream_tests)

    add_executable(progress_tests
        tests/progress_test.cpp
        ${SLH_CORE_SOURCES})
    target_include_directories(progress_tests PRIVATE
        ${NATIVERENDER_ROOT_PATH}
        ${NATIVERENDER_ROOT_PATH}/tests)
    target_compile_definitions(progress_tests PRIVATE
        FIXTURE_DIR="${NATIVERENDER_ROOT_PATH}/../../../../fixtures/synthetic")
    add_test(NAME ProgressTests COMMAND progress_tests)

    add_executable(transport_tests
        tests/transport_test.cpp
        ${SLH_CORE_SOURCES})
    target_include_directories(transport_tests PRIVATE
        ${NATIVERENDER_ROOT_PATH}
        ${NATIVERENDER_ROOT_PATH}/tests)
    target_compile_definitions(transport_tests PRIVATE
        FIXTURE_DIR="${NATIVERENDER_ROOT_PATH}/../../../../fixtures/synthetic")
    add_test(NAME TransportTests COMMAND transport_tests)

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
