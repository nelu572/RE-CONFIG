#!/usr/bin/env python3
"""Generate a temporary 130 BPM WAV speaker kick for ROOM 04.

The game can move this to procedural playback later. For now this script keeps
the sound design parameters in one place and writes simple 16-bit mono WAVs.
"""

from __future__ import annotations

import argparse
import math
import random
import struct
import wave
from pathlib import Path


SAMPLE_RATE = 44100
TAU = math.pi * 2.0
SPEAKER_BPM = 130.0
BEAT_SECONDS = 60.0 / SPEAKER_BPM


VARIANTS = {
    "kick": {
        "duration": BEAT_SECONDS,
        "kick_decay": 0.235,
        "start_hz": 92.0,
        "end_hz": 38.0,
        "body_hz": 49.0,
        "click": 0.030,
        "zap": 0.085,
        "noise": 0.006,
        "body": 0.72,
        "drive": 1.22,
        "gain": 0.88,
    },
}


def clamp(value: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, value))


def falloff(progress: float, power: float) -> float:
    return max(0.0, 1.0 - progress) ** power


def synth_pulse(params: dict[str, float], seed: int) -> list[float]:
    rng = random.Random(seed)
    count = int(params["duration"] * SAMPLE_RATE)
    kick_count = max(1, int(params["kick_decay"] * SAMPLE_RATE))
    samples: list[float] = []
    phase = 0.0
    noise_state = 0.0

    for i in range(count):
        t = i / SAMPLE_RATE
        p = min(1.0, i / max(1, kick_count - 1))

        pitch_curve = falloff(p, 2.15)
        hz = params["end_hz"] + (params["start_hz"] - params["end_hz"]) * pitch_curve
        phase += TAU * hz / SAMPLE_RATE

        thump_env = falloff(p, 3.15)
        body_env = falloff(p, 4.2)
        attack = clamp(t / 0.006, 0.0, 1.0)
        click_env = falloff(clamp(t / 0.008, 0.0, 1.0), 4.4)
        zap_env = falloff(clamp(t / 0.018, 0.0, 1.0), 3.0)
        noise_env = falloff(clamp(t / 0.026, 0.0, 1.0), 3.0)

        thump = math.sin(phase) * thump_env
        harmonic = math.sin(phase * 2.0 + 0.18) * thump_env * 0.045
        body = math.sin(TAU * params["body_hz"] * t) * body_env * params["body"]
        click = (rng.random() * 2.0 - 1.0) * click_env * params["click"]
        zap = math.sin(TAU * (760.0 - 480.0 * clamp(t / 0.018, 0.0, 1.0)) * t) * zap_env * params["zap"]
        noise_state = noise_state * 0.82 + (rng.random() * 2.0 - 1.0) * 0.18
        air = noise_state * noise_env * params["noise"]

        mixed = (thump + harmonic + body) * attack + click + zap + air
        driven = math.tanh(mixed * params["drive"]) / math.tanh(params["drive"])
        samples.append(driven)

    peak = max(max(abs(s) for s in samples), 0.001)
    return [s / peak * params["gain"] for s in samples]


def write_wav(path: Path, samples: list[float]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(SAMPLE_RATE)
        frames = bytearray()
        for sample in samples:
            value = int(clamp(sample, -1.0, 1.0) * 32767)
            frames.extend(struct.pack("<h", value))
        wav.writeframes(bytes(frames))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--out",
        type=Path,
        default=Path("tools") / "audio_out",
        help="Output directory for generated WAV files.",
    )
    parser.add_argument(
        "--variant",
        choices=[*VARIANTS.keys(), "all"],
        default="kick",
        help="Which pulse variant to generate.",
    )
    args = parser.parse_args()

    names = VARIANTS.keys() if args.variant == "all" else [args.variant]
    for index, name in enumerate(names):
        path = args.out / f"speaker_{name}_130bpm.wav"
        write_wav(path, synth_pulse(VARIANTS[name], seed=144 + index))
        print(path)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
