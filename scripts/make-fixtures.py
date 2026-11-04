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
