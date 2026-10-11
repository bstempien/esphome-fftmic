#!/usr/bin/env python3
"""Receive INMP441 UDP PCM packets or analyze a recorded mono WAV."""

import argparse
import array
import math
import socket
import struct
import statistics
import sys
import wave
from pathlib import Path

SAMPLE_RATE = 16000
PACKET_HEADER = struct.Struct("!4sI")
PACKET_MAGIC = b"FFT1"
MAX_REORDER_PACKETS = 64


def fft_magnitudes(frame, window):
    """Return the magnitudes of the positive-frequency bins of a real frame."""
    size = len(frame)
    values = [complex(sample * window[index], 0.0) for index, sample in enumerate(frame)]

    reversed_index = 0
    for index in range(1, size):
        bit = size >> 1
        while reversed_index & bit:
            reversed_index ^= bit
            bit >>= 1
        reversed_index ^= bit
        if index < reversed_index:
            values[index], values[reversed_index] = values[reversed_index], values[index]

    length = 2
    while length <= size:
        root = complex(math.cos(-2.0 * math.pi / length), math.sin(-2.0 * math.pi / length))
        half_length = length // 2
        for start in range(0, size, length):
            twiddle = 1.0 + 0.0j
            for offset in range(half_length):
                even = start + offset
                odd = even + half_length
                product = values[odd] * twiddle
                values[odd] = values[even] - product
                values[even] += product
                twiddle *= root
        length <<= 1
    return [abs(value) for value in values[: size // 2 + 1]]


def record(args):
    output = Path(args.output)
    pending = {}
    expected_sample = None
    received_packets = 0
    missing_samples = 0
    late_packets = 0

    with wave.open(str(output), "wb") as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(SAMPLE_RATE)
        print(f"Listening on {args.bind}:{args.port}; writing {output}")
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.bind((args.bind, args.port))
                while True:
                    datagram, _ = sock.recvfrom(2048)
                    if len(datagram) < PACKET_HEADER.size:
                        continue
                    magic, first_sample = PACKET_HEADER.unpack_from(datagram)
                    payload = datagram[PACKET_HEADER.size :]
                    if magic != PACKET_MAGIC or not payload or len(payload) % 2:
                        continue
                    if expected_sample is None:
                        expected_sample = first_sample
                    if first_sample < expected_sample:
                        late_packets += 1
                        continue
                    pending[first_sample] = payload
                    received_packets += 1

                    while expected_sample in pending:
                        block = pending.pop(expected_sample)
                        wav_file.writeframesraw(block)
                        expected_sample += len(block) // 2

                    if len(pending) > MAX_REORDER_PACKETS:
                        next_sample = min(pending)
                        missing = next_sample - expected_sample
                        if missing > 0:
                            wav_file.writeframesraw(b"\0\0" * missing)
                            missing_samples += missing
                            expected_sample = next_sample
        except KeyboardInterrupt:
            pass
        finally:
            if expected_sample is not None:
                for first_sample in sorted(pending):
                    if first_sample < expected_sample:
                        continue
                    gap = first_sample - expected_sample
                    if gap:
                        wav_file.writeframesraw(b"\0\0" * gap)
                        missing_samples += gap
                    block = pending[first_sample]
                    wav_file.writeframesraw(block)
                    expected_sample = first_sample + len(block) // 2

    print(
        f"Saved {output}; received {received_packets} packets, "
        f"filled {missing_samples} missing samples, ignored {late_packets} late packets"
    )


def analyze(args):
    with wave.open(args.input, "rb") as wav_file:
        if wav_file.getnchannels() != 1 or wav_file.getsampwidth() != 2:
            raise SystemExit("Input must be 16-bit mono PCM WAV.")
        sample_rate = wav_file.getframerate()
        if sample_rate != SAMPLE_RATE:
            raise SystemExit(f"Input sample rate must be {SAMPLE_RATE} Hz, got {sample_rate} Hz.")
        pcm = array.array("h")
        pcm.frombytes(wav_file.readframes(wav_file.getnframes()))
        if pcm.itemsize != 2:
            raise SystemExit("This platform does not provide 16-bit signed short samples.")
        if sys.byteorder != "little":
            pcm.byteswap()

    fft_size = 1024
    hop_size = 512
    window = [0.5 - 0.5 * math.cos(2.0 * math.pi * index / (fft_size - 1)) for index in range(fft_size)]
    usable_bins = range(60 * fft_size // sample_rate, 4000 * fft_size // sample_rate + 1)
    frame_notes = []
    for start in range(0, max(0, len(pcm) - fft_size + 1), hop_size):
        frame = pcm[start : start + fft_size]
        rms = math.sqrt(sum(sample * sample for sample in frame) / fft_size)
        if rms < args.min_rms:
            frame_notes.append(None)
            continue
        spectrum = fft_magnitudes(frame, window)
        peak_bin = max(usable_bins, key=lambda index: spectrum[index])
        offset = 0.0
        if 0 < peak_bin < len(spectrum) - 1:
            left, center, right = (
                math.log(spectrum[index] + 1.0e-12) for index in (peak_bin - 1, peak_bin, peak_bin + 1)
            )
            denominator = left - 2 * center + right
            if abs(denominator) > 1.0e-9:
                offset = 0.5 * (left - right) / denominator
        frame_notes.append((peak_bin + offset) * sample_rate / fft_size)

    events = []
    active = []
    for frequency in frame_notes + [None]:
        same_note = (
            bool(active)
            and frequency is not None
            and abs(frequency - active[-1]) <= max(12.0, active[-1] * 0.04)
        )
        if same_note:
            active.append(frequency)
            continue
        if active:
            duration_ms = len(active) * hop_size * 1000 / sample_rate
            if duration_ms >= args.min_duration_ms:
                events.append((statistics.median(active), duration_ms))
            active = []
        if frequency is not None:
            active = [frequency]

    if not events:
        raise SystemExit("No sustained pitched notes found; try lowering --min-rms or --min-duration-ms.")

    lines = [
        "# Generated from analyzed audio. Review frequencies and durations before using.",
        "notes:",
    ]
    for frequency, duration_ms in events:
        lines.extend(
            [
                f"  - frequency: {frequency:.1f}",
                f"    min_duration: {max(64, round(duration_ms * 0.5))}ms",
            ]
        )
    result = "\n".join(lines) + "\n"
    if args.output:
        Path(args.output).write_text(result, encoding="utf-8")
        print(f"Wrote {args.output}")
    else:
        sys.stdout.write(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)

    record_parser = commands.add_parser("record", help="receive UDP stream and save a WAV")
    record_parser.add_argument("--bind", default="0.0.0.0", help="local IPv4 address to bind")
    record_parser.add_argument("--port", type=int, default=5005)
    record_parser.add_argument("--output", required=True, help="output WAV path")
    record_parser.set_defaults(func=record)

    analyze_parser = commands.add_parser("analyze", help="extract sustained target notes from a WAV")
    analyze_parser.add_argument("input", help="16-bit mono, 16000 Hz WAV file")
    analyze_parser.add_argument("-o", "--output", help="write note YAML to this file instead of stdout")
    analyze_parser.add_argument("--min-rms", type=float, default=500)
    analyze_parser.add_argument("--min-duration-ms", type=float, default=80)
    analyze_parser.set_defaults(func=analyze)

    args = parser.parse_args()
    if args.command == "record" and not 1 <= args.port <= 65535:
        parser.error("--port must be between 1 and 65535")
    args.func(args)


if __name__ == "__main__":
    main()
