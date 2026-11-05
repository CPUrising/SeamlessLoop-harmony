/*
 * dr_libs 实现单元。
 *
 * dr_wav.h / dr_mp3.h 都是 header-only 库，必须恰好在一个 translation unit 中
 * 定义实现宏，否则会重复定义符号或链接不到实现。
 *
 * 本变更（0001）只承诺 WAV 与 MP3 两种格式，因此只编译这两个解码器。
 * FLAC / Vorbis 等格式留待后续 change 评估，不在此引入。
 */

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
