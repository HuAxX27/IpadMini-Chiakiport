#!/usr/bin/env python3
# A4.1: checks that the iPad's Opus decoder (ios/Audio/ck_audio_decoder.c, run through opus_wav)
# writes the right WAV: a valid header, as many samples as ffmpeg decodes, the same samples as
# ffmpeg with libopus (the library the iPad links; equal to the bit on the same host), and close
# to ffmpeg's own Opus decoder, which shares no code with libopus.
# Usage: check_decode.py FILE.opus... -- TOOL...   TOOL runs opus_wav, maybe under qemu.
import struct
import subprocess
import sys
import tempfile

# Signal to error, in dB. libopus is the same library on both sides, but an armv7 build under qemu
# against an x86_64 ffmpeg may round a few samples differently. ffmpeg's own decoder is here to
# catch a mistake both libopus runs would share; it does not conceal a lost frame (it fades back in
# from silence over about 120 ms), so the frames from each loss to AFTER_LOSS later are left out of
# that comparison.
MIN_SNR_LIBOPUS = 60.0
MIN_SNR_NATIVE = 20.0
AFTER_LOSS = 48000 * 200 // 1000


def run(args):
    result = subprocess.run(args, capture_output=True, check=False)
    if result.returncode != 0 or result.stderr:
        sys.exit(f"{' '.join(args)} failed ({result.returncode}): {result.stderr.decode(errors='replace')}")
    return result.stdout


def read_wav(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 44 or data[0:4] != b"RIFF" or data[8:16] != b"WAVEfmt " or data[36:40] != b"data":
        sys.exit(f"{path}: not a canonical WAV")
    riff_size, fmt_size, fmt, channels, rate, byte_rate, align, bits, data_size = struct.unpack(
        "<I4x4xIHHIIHH4xI", data[4:44])
    if (fmt_size, fmt, rate, bits) != (16, 1, 48000, 16) or channels not in (1, 2):
        sys.exit(f"{path}: fmt {fmt_size} {fmt} {channels} ch {rate} Hz {bits} bits")
    if byte_rate != rate * channels * 2 or align != channels * 2:
        sys.exit(f"{path}: byte rate {byte_rate} or block align {align} do not match")
    if riff_size != 36 + data_size or data_size != len(data) - 44:
        sys.exit(f"{path}: sizes {riff_size}/{data_size} do not match the file ({len(data)} bytes)")
    return channels, data[44:]


def without(pcm, channels, spans):
    """pcm without the [start, end) frame spans, which are sorted."""
    width = channels * 2
    kept, at = [], 0
    for start, end in spans:
        kept.append(pcm[at * width:start * width])
        at = max(at, end)
    kept.append(pcm[at * width:])
    return b"".join(kept)


def snr(ours, ref):
    """Signal to error in dB and the largest difference, over 16-bit little-endian samples."""
    try:
        import numpy
        a = numpy.frombuffer(ours, dtype="<i2").astype(numpy.float64)
        b = numpy.frombuffer(ref, dtype="<i2").astype(numpy.float64)
        d = a - b
        signal, error, worst = float((b * b).sum()), float((d * d).sum()), int(abs(d).max(initial=0))
    except ImportError:
        a = struct.unpack(f"<{len(ours) // 2}h", ours)
        b = struct.unpack(f"<{len(ref) // 2}h", ref)
        signal = error = worst = 0
        for x, y in zip(a, b):
            d = x - y
            signal += y * y
            error += d * d
            worst = max(worst, abs(d))
    if error == 0:
        return float("inf"), 0
    import math
    return 10.0 * math.log10(max(signal, 1) / error), worst


def check(path, tool, tmp):
    wav = f"{tmp}/out.wav"
    out = run(tool + [path, wav]).decode().split()
    info = dict(item.split("=", 1) for item in out)
    lost_at = [int(v) for v in info["lost_at"].split(",") if v]
    channels, ours = read_wav(wav)
    if int(info["frames"]) * channels * 2 != len(ours):
        sys.exit(f"{path}: the tool says {info['frames']} frames, the WAV has {len(ours) // (channels * 2)}")
    lost = int(info["marked_lost"])
    if int(info["concealed"]) != lost or int(info["errors"]) != 0:
        sys.exit(f"{path}: {info}")

    report = []
    spans = [(start, start + AFTER_LOSS) for start in lost_at]
    for codec, minimum in (("libopus", MIN_SNR_LIBOPUS), ("opus", MIN_SNR_NATIVE)):
        ref = run(["ffmpeg", "-v", "error", "-c:a", codec, "-i", path, "-f", "s16le", "-ac", str(channels),
                   "-ar", "48000", "-"])
        if len(ref) != len(ours):
            sys.exit(f"{path}: ffmpeg ({codec}) decodes {len(ref) // (channels * 2)} frames, "
                     f"the WAV has {len(ours) // (channels * 2)}")
        if codec == "libopus":
            value, worst = snr(ours, ref)
        else:
            value, worst = snr(without(ours, channels, spans), without(ref, channels, spans))
        if value < minimum:
            sys.exit(f"{path}: {value:.1f} dB from ffmpeg ({codec}), under {minimum} dB; largest difference {worst}")
        report.append(f"{codec} {'exact' if worst == 0 else f'{value:.1f} dB'}")
    seconds = int(info["frames"]) / 48000
    print(f"{path}: {seconds:.2f} s, {channels} ch, {info['packets']} packets, {lost} lost and concealed; "
          f"ffmpeg: {', '.join(report)}")


def main():
    if "--" not in sys.argv:
        sys.exit("usage: check_decode.py FILE.opus... -- TOOL...")
    split = sys.argv.index("--")
    files, tool = sys.argv[1:split], sys.argv[split + 1:]
    with tempfile.TemporaryDirectory(prefix="ck-decode-") as tmp:
        for path in files:
            check(path, tool, tmp)


if __name__ == "__main__":
    main()
