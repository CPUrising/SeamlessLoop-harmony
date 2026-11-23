#!/usr/bin/env python3
"""生成合成测试音频 fixtures（相位连续的循环样本）。

设计要点：
  循环区间在数学上严格相位连续 —— 循环体长度是所选频率整数个周期，
  因此 LoopEnd 处的波形相位与 LoopStart 完全一致。这样"跨越 N 次边界
  零突变"可以用数值断言，而不是靠耳朵猜。

用法：
  python scripts/make-fixtures.py
"""

import json
import subprocess
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "fixtures" / "synthetic"


def build_signal(sample_rate: int, channels: int, intro_samples: int, loop_samples: int):
    """构造 Intro + Loop 两段。

    Intro: 220Hz 基频 + 淡入，长度 intro_samples。
    Loop : 441Hz(或按采样率缩放) 基频 + 二次谐波 + 周期包络，长度 loop_samples。
    """
    total = intro_samples + loop_samples
    t = np.arange(total, dtype=np.float64) / sample_rate

    sig = np.zeros(total, dtype=np.float64)

    # ---- Intro 段 ----
    intro_freq = 220.0
    intro = np.sin(2.0 * np.pi * intro_freq * t[:intro_samples])
    # 淡入 100ms，避免开头爆音（这不是循环边界，只是素材起始）
    fade = min(int(0.1 * sample_rate), intro_samples)
    if fade > 0:
        intro[:fade] *= np.linspace(0.0, 1.0, fade)
    sig[:intro_samples] = 0.5 * intro

    # ---- Loop 段 ----
    # 频率选择：让 period = sample_rate / freq 为整数，且 loop_samples 是其整数倍
    # 44100 -> 441Hz (period 100)；48000 -> 480Hz (period 100)
    loop_freq = sample_rate / 100.0
    n = np.arange(loop_samples, dtype=np.float64)
    phase = 2.0 * np.pi * loop_freq * n / sample_rate
    body = np.sin(phase) + 0.35 * np.sin(2.0 * phase)

    # 周期包络：周期数必须整除 loop_samples，保证 LoopEnd 与 LoopStart 包络值一致
    envelope_cycles = 2
    assert loop_samples % envelope_cycles == 0
    env = 0.75 + 0.25 * np.sin(
        2.0 * np.pi * envelope_cycles * n / loop_samples - np.pi / 2.0
    )
    sig[intro_samples:] = 0.5 * body * env

    # 归一化到 -0.9 ~ 0.9，避免削顶
    peak = np.max(np.abs(sig))
    if peak > 0:
        sig *= 0.9 / peak

    # 复制到多声道
    if channels == 1:
        return sig.astype(np.float64)
    return np.repeat(sig[:, None], channels, axis=1)


def write_wav(path: Path, data: np.ndarray, sample_rate: int):
    """写 16-bit PCM WAV（不依赖 scipy）。"""
    if data.ndim == 1:
        channels = 1
    else:
        channels = data.shape[1]

    pcm = np.clip(data, -1.0, 1.0)
    pcm = (pcm * 32767.0).astype("<i2")

    with open(path, "wb") as f:
        n_frames = data.shape[0]
        block_align = channels * 2
        byte_rate = sample_rate * block_align
        data_bytes = pcm.tobytes()

        f.write(b"RIFF")
        f.write((36 + len(data_bytes)).to_bytes(4, "little"))
        f.write(b"WAVE")
        f.write(b"fmt ")
        f.write((16).to_bytes(4, "little"))
        f.write((1).to_bytes(2, "little"))          # PCM
        f.write(channels.to_bytes(2, "little"))
        f.write(sample_rate.to_bytes(4, "little"))
        f.write(byte_rate.to_bytes(4, "little"))
        f.write(block_align.to_bytes(2, "little"))
        f.write((16).to_bytes(2, "little"))         # bits per sample
        f.write(b"data")
        f.write(len(data_bytes).to_bytes(4, "little"))
        f.write(data_bytes)


def probe_duration(path: Path):
    """用 ffprobe 读取解码后的时长与采样率，用于 MP3 的实测记录。"""
    try:
        out = subprocess.run(
            [
                "ffprobe", "-v", "error",
                "-select_streams", "a:0",
                "-show_entries", "stream=sample_rate,channels,duration",
                "-of", "json", str(path),
            ],
            capture_output=True, text=True, check=True,
        )
        info = json.loads(out.stdout)["streams"][0]
        return {
            "sample_rate": int(info.get("sample_rate", 0)),
            "channels": int(info.get("channels", 0)),
            "duration": float(info.get("duration", 0.0)),
        }
    except Exception as exc:  # noqa: BLE001
        return {"error": str(exc)}


def make_one(name, sample_rate, channels, intro_sec, loop_sec):
    intro_samples = int(round(intro_sec * sample_rate))
    loop_samples = int(round(loop_sec * sample_rate))

    data = build_signal(sample_rate, channels, intro_samples, loop_samples)
    wav_path = OUT / f"{name}.wav"
    write_wav(wav_path, data, sample_rate)

    total = intro_samples + loop_samples
    entry = {
        "file": wav_path.name,
        "format": "wav",
        "sample_rate": sample_rate,
        "channels": channels,
        "total_samples": total,
        "loop_start": intro_samples,
        "loop_end": total,
        "loop_length": loop_samples,
        "phase_continuous": True,
        "note": "循环体为整数周期波形，LoopStart/LoopEnd 相位一致",
    }
    print(f"[wav] {wav_path.name}  sr={sample_rate} ch={channels} "
          f"total={total} loop=[{intro_samples},{total})")
    return entry, wav_path


def build_ab_pair(sample_rate: int, channels: int, a_samples: int, b_samples: int):
    """构造一对内容不同、边界相位连续的 A/B 素材。

    A（前奏）：100Hz 基频 + 淡入，长度 a_samples。
    B（循环体）：441Hz 基频 + 二次谐波 + 周期包络，长度 b_samples。

    相位连续性的保证：
    - A 的周期取 441 采样（44.1kHz 下 100Hz），且 a_samples 必须是 441 的整数倍，
      这样 A 的末帧相位只差一个采样步长，值接近 0。
    - B 的首帧 sin(0) = 0，因此 A→B 边界突变接近 0。
    - B 的长度是 441Hz（周期 100）的整数倍，且包络周期整除 b_samples，
      因此 B 末帧与首帧的相位差同样只有一个采样步长。
    """
    # A 段：周期 441 采样，保证 a_samples 是整数个周期
    a_period = 441
    assert a_samples % a_period == 0, (
        f"a_samples={a_samples} 必须是 {a_period} 的整数倍，否则 A 末帧相位不为 0"
    )
    a_freq = sample_rate / float(a_period)
    na = np.arange(a_samples, dtype=np.float64)
    a = np.sin(2.0 * np.pi * a_freq * na / sample_rate)
    # 淡入 100ms，避免文件起始爆音（不是循环边界）
    fade = min(int(0.1 * sample_rate), a_samples)
    if fade > 0:
        a[:fade] *= np.linspace(0.0, 1.0, fade)
    a *= 0.5

    # B 段：period = sample_rate / 441 = 100（44100 时）
    b_freq = sample_rate / 100.0
    nb = np.arange(b_samples, dtype=np.float64)
    phase = 2.0 * np.pi * b_freq * nb / sample_rate
    body = np.sin(phase) + 0.35 * np.sin(2.0 * phase)

    envelope_cycles = 2
    assert b_samples % envelope_cycles == 0
    env = 0.75 + 0.25 * np.sin(
        2.0 * np.pi * envelope_cycles * nb / b_samples - np.pi / 2.0
    )
    b = 0.5 * body * env

    # 两段各自归一化到 -0.9 ~ 0.9，避免削顶；归一化不改变首末帧为 0 的性质
    for seg in (a, b):
        peak = np.max(np.abs(seg))
        if peak > 0:
            seg *= 0.9 / peak

    def to_channels(sig):
        if channels == 1:
            return sig
        return np.repeat(sig[:, None], channels, axis=1)

    return to_channels(a), to_channels(b)


def make_ab_pair(name, sample_rate, channels, a_sec, b_sec):
    """生成一对 A/B WAV，并返回两条 manifest 条目。"""
    a_samples = int(round(a_sec * sample_rate))
    b_samples = int(round(b_sec * sample_rate))

    a_data, b_data = build_ab_pair(sample_rate, channels, a_samples, b_samples)

    a_path = OUT / f"{name}_a.wav"
    b_path = OUT / f"{name}_b.wav"
    write_wav(a_path, a_data, sample_rate)
    write_wav(b_path, b_data, sample_rate)

    total = a_samples + b_samples
    common = {
        "format": "wav",
        "sample_rate": sample_rate,
        "channels": channels,
        "ab_pair": name,
        "phase_continuous": True,
    }
    a_entry = dict(common)
    a_entry.update({
        "file": a_path.name,
        "role": "a",
        "total_samples": a_samples,
        "note": "A 段（前奏）：100Hz（周期 441 采样），末帧相位接近 0，与 B 首帧连续",
    })
    b_entry = dict(common)
    b_entry.update({
        "file": b_path.name,
        "role": "b",
        "total_samples": b_samples,
        "note": "B 段（循环体）：441Hz + 二次谐波，首末帧相位一致",
    })

    print(f"[ab] {a_path.name} ({a_samples}) + {b_path.name} ({b_samples}) "
          f"-> virtual total={total} loop=[{a_samples},{total})")
    return a_entry, b_entry


def make_ogg(name, wav_path: Path, sample_rate):
    """把 WAV 转成 OGG Vorbis，用于验证 OGG 解码路径。"""
    ogg_path = OUT / f"{name}.ogg"
    subprocess.run(
        [
            "ffmpeg", "-y", "-v", "error",
            "-i", str(wav_path),
            "-codec:a", "libvorbis", "-q:a", "6",
            str(ogg_path),
        ],
        check=True,
    )
    probed = probe_duration(ogg_path)
    print(f"[ogg] {ogg_path.name}  probe={probed}")
    return {
        "file": ogg_path.name,
        "format": "ogg",
        "sample_rate": probed.get("sample_rate", sample_rate),
        "channels": probed.get("channels", 1),
        "decoded_duration": probed.get("duration"),
        "derived_from": wav_path.name,
        "phase_continuous": True,
        "note": (
            "OGG Vorbis 由源 WAV 转码。容器 granule position 与解码器输出"
            "可能有 0~2 帧差异，循环点须在解码后实测。"
        ),
    }


def make_mp3(name, wav_path: Path, sample_rate):
    mp3_path = OUT / f"{name}.mp3"
    subprocess.run(
        [
            "ffmpeg", "-y", "-v", "error",
            "-i", str(wav_path),
            "-codec:a", "libmp3lame", "-b:a", "192k",
            str(mp3_path),
        ],
        check=True,
    )
    probed = probe_duration(mp3_path)
    print(f"[mp3] {mp3_path.name}  probe={probed}")
    return {
        "file": mp3_path.name,
        "format": "mp3",
        "sample_rate": probed.get("sample_rate", sample_rate),
        "channels": probed.get("channels", 1),
        "decoded_duration": probed.get("duration"),
        "derived_from": wav_path.name,
        "phase_continuous": True,
        "note": (
            "MP3 存在编码器延迟/填充，解码后的采样位置与源 WAV 不同。"
            "循环点须在解码后实测，不可直接沿用源 WAV 的数值。"
        ),
    }


def main():
    OUT.mkdir(parents=True, exist_ok=True)

    entries = []

    e1, wav1 = make_one("intro_loop_44100_mono", 44100, 1, 0.75, 2.0)
    entries.append(e1)

    e2, wav2 = make_one("intro_loop_48000_stereo", 48000, 2, 0.75, 2.0)
    entries.append(e2)

    entries.append(make_mp3("intro_loop_44100_mono", wav1, 44100))
    entries.append(make_ogg("intro_loop_44100_mono", wav1, 44100))

    # A/B 对：A 0.75s（前奏）+ B 2.0s（循环体），虚拟轴总长 2.75s
    a_entry, b_entry = make_ab_pair("ab_pair_44100_stereo", 44100, 2, 0.75, 2.0)
    entries.append(a_entry)
    entries.append(b_entry)

    manifest = {
        "generated_by": "scripts/make-fixtures.py",
        "purpose": "采样级无缝循环的自动化断言与听感基准",
        "synthetic_only": True,
        "items": entries,
    }
    manifest_path = ROOT / "fixtures" / "manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(f"\nmanifest -> {manifest_path}")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as exc:
        print(f"外部命令失败: {exc}", file=sys.stderr)
        sys.exit(1)
