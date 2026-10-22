#!/usr/bin/env python3
# A2.5: checks with Python's plistlib that the registry Bridge/ck_registry.c writes is a plist as
# Apple's parsers read it, and that ck_registry reads back a plist written by someone else (plistlib
# writes the same XML as NSPropertyListSerialization: sorted keys, data wrapped and indented).
# Usage: check_registry_plist.py TOOL...   the command that runs ck-registry-tool, maybe under qemu.
import os
import plistlib
import subprocess
import sys
import tempfile

PS5_1 = 1000100
PS4_10 = 1000

EXPECTED = {
    "0A1B2C3D4E5F": {
        "host": "192.168.0.10",
        "rp_key": b"\xf0" * 16,
        "rp_key_type": 2,
        "rp_regist_key": b"1a2b3c4d" + b"\0" * 8,
        "server_mac": bytes.fromhex("0a1b2c3d4e5f"),
        "server_nickname": "Sala & <PS5> ñ",
        "target": PS5_1,
    },
    "0A1B2C3D4E60": {
        "host": "",
        "rp_key": b"\x0f" * 16,
        "rp_key_type": 0,
        "rp_regist_key": b"FFFFFFFF00000000",
        "server_mac": bytes.fromhex("0a1b2c3d4e60"),
        "server_nickname": "Cuarto",
        "target": PS4_10,
    },
}


def dump_line(key, entry):
    return "|".join([
        key,
        str(entry["target"]),
        entry["server_nickname"],
        entry["rp_regist_key"].ljust(16, b"\0").hex(),
        str(entry["rp_key_type"]),
        entry["rp_key"].hex(),
        entry["host"],
    ])


def fail(message):
    print("check_registry_plist: " + message, file=sys.stderr)
    sys.exit(1)


def main():
    tool = sys.argv[1:]
    if not tool:
        fail("falta el comando de ck-registry-tool")
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "consoles.plist")

        # What ck_registry writes, as plistlib reads it.
        subprocess.run(tool + ["write", path], check=True)
        with open(path, "rb") as f:
            written = plistlib.load(f, fmt=plistlib.FMT_XML)
        if written != EXPECTED:
            fail("plistlib lee otra cosa:\n%r" % (written,))
        print("ok: plistlib lee el plist de ck_registry")

        # What plistlib writes, with keys of the app next to the consoles and inside them.
        rewritten = {key: dict(entry) for key, entry in EXPECTED.items()}
        rewritten["0A1B2C3D4E5F"]["favorite"] = True
        rewritten["0A1B2C3D4E5F"]["streams"] = [{"fps": 30}, [], "x"]
        # The app may write the key unpadded; it reads back NUL-padded.
        rewritten["0A1B2C3D4E5F"]["rp_regist_key"] = b"1a2b3c4d"
        rewritten["order"] = ["0A1B2C3D4E60", "0A1B2C3D4E5F"]
        with open(path, "wb") as f:
            plistlib.dump(rewritten, f, fmt=plistlib.FMT_XML, sort_keys=True)
        out = subprocess.run(tool + ["dump", path], check=True, capture_output=True, text=True).stdout
        # plistlib sorts the keys, so the consoles come back in key order.
        expected = "".join(dump_line(key, EXPECTED[key]) + "\n" for key in sorted(EXPECTED))
        if out != expected:
            fail("ck_registry lee otra cosa del plist de plistlib:\n%s\nen vez de:\n%s" % (out, expected))
        print("ok: ck_registry lee el plist de plistlib")

        # A binary plist is refused, not misread: whoever rewrites the file keeps it in XML.
        with open(path, "wb") as f:
            plistlib.dump(rewritten, f, fmt=plistlib.FMT_BINARY)
        if subprocess.run(tool + ["dump", path], capture_output=True).returncode == 0:
            fail("ck_registry aceptó un plist binario")
        print("ok: ck_registry rechaza el plist binario")


if __name__ == "__main__":
    main()
