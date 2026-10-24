#!/usr/bin/env python3
# A2.8: checks with ffmpeg that what Bridge/ck_recorder.c writes plays outside the port: the .opus
# decodes without errors to exactly the samples recorded, lost frames included, and the .h264
# decodes to the same frames as the sample it was recorded from.
# Usage: check_recording.py SAMPLE.h264 TOOL...   the command that runs ck-recorder-tool, maybe
# under qemu.
import os
import subprocess
import sys
import tempfile

SECONDS = 3


def run(args):
    result = subprocess.run(args, capture_output=True, check=False)
    if result.returncode != 0 or result.stderr:
        sys.exit(f"{' '.join(args)} failed ({result.returncode}): {result.stderr.decode(errors='replace')}")
    return result.stdout


def frames(path):
    out = run(["ffprobe", "-v", "error", "-count_frames", "-select_streams", "v:0",
               "-show_entries", "stream=nb_read_frames", "-of", "csv=p=0", path])
    return int(out.decode().strip())


def main():
    sample = sys.argv[1]
    tool = sys.argv[2:]
    with tempfile.TemporaryDirectory(prefix="ck-recording-") as tmp:
        base = os.path.join(tmp, "session")
        out = run(tool + [base, sample, str(SECONDS)]).decode().split()
        info = dict(zip(out[0::2], (int(v) for v in out[1::2])))
        if info["samples"] != SECONDS * 48000 or info["lost"] == 0:
            sys.exit(f"the tool recorded {info}")

        stream = run(["ffprobe", "-v", "error", "-select_streams", "a:0",
                      "-show_entries", "stream=codec_name,channels,sample_rate",
                      "-of", "default=noprint_wrappers=1", base + ".opus"]).decode().split()
        if sorted(stream) != ["channels=2", "codec_name=opus", "sample_rate=48000"]:
            sys.exit(f"ffprobe sees {stream}")
        pcm = run(["ffmpeg", "-v", "error", "-i", base + ".opus", "-f", "s16le", "-"])
        if len(pcm) != info["samples"] * 2 * 2:
            sys.exit(f"ffmpeg decoded {len(pcm) // 4} samples, not {info['samples']}")

        with open(sample, "rb") as a, open(base + ".h264", "rb") as b:
            if a.read() != b.read():
                sys.exit("the .h264 is not the sample")
        run(["ffmpeg", "-v", "error", "-i", base + ".h264", "-f", "null", "-"])
        if frames(base + ".h264") != frames(sample):
            sys.exit("the .h264 has a different number of frames")
    print(f"recording plays in ffmpeg: {info['samples']} samples with {info['lost']} lost frames")


if __name__ == "__main__":
    main()
