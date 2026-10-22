#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
"""Test receiver for the ESP32 bridge: shows the DualSense state it sends.

Listens on UDP port 9400 (contract C4, bridge/protocol/ck_pad_packet.h) and
prints the buttons, sticks and triggers whenever the buttons change and twice
a second, with the packets per second, losses and the longest gap between
packets. Only needs Python 3, on Windows, Linux or macOS.

    python3 ck_pad_recv.py            # a line per button change, two per second
    python3 ck_pad_recv.py --log      # one line per packet
"""

import argparse
import socket
import struct
import sys
import time

PORT = 9400
PACKET = struct.Struct("<2sBBHIBBhhhh")  # 20 bytes, little-endian
MAGIC = b"CK"
VERSION = 1
RESYNC_S = 0.5  # CK_PAD_RESYNC_MS
FLAG_TOUCHPAD = 0x01

BUTTONS = [
    "Cruz", "Circulo", "Cuadrado", "Triangulo",
    "Izq", "Der", "Arriba", "Abajo",
    "L1", "R1", "L3", "R3",
    "Options", "Create", "Touchpad", "PS",
]

assert PACKET.size == 20


def lan_address():
    """Best guess of this computer's LAN address. Sends nothing."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("192.0.2.1", 9))
        return s.getsockname()[0]
    except OSError:
        return None
    finally:
        s.close()


def decode(data):
    if len(data) != PACKET.size:
        return None
    magic, version, flags, seq, buttons, l2, r2, lx, ly, rx, ry = PACKET.unpack(data)
    if magic != MAGIC or version != VERSION:
        return None
    return dict(flags=flags, seq=seq, buttons=buttons, l2=l2, r2=r2, lx=lx, ly=ly, rx=rx, ry=ry)


def seq_newer(seq, last):
    diff = (seq - last) & 0xFFFF
    return 0 < diff < 0x8000


def describe(p):
    pressed = [name for bit, name in enumerate(BUTTONS) if p["buttons"] & (1 << bit)]
    if p["flags"] & FLAG_TOUCHPAD and "Touchpad" not in pressed:
        pressed.append("Touchpad(flag)")
    return "seq {:5d} | L {:6d},{:6d} | R {:6d},{:6d} | L2 {:3d} R2 {:3d} | {}".format(
        p["seq"], p["lx"], p["ly"], p["rx"], p["ry"], p["l2"], p["r2"],
        " ".join(pressed) if pressed else "-")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", type=int, default=PORT, help="UDP port (9400)")
    parser.add_argument("--log", action="store_true", help="print every packet on its own line")
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.bind(("0.0.0.0", args.port))
    except OSError as e:
        sys.exit("No se pudo abrir el puerto UDP {}: {}".format(args.port, e))
    sock.settimeout(0.1)

    ip = lan_address()
    print("Escuchando en el puerto UDP {}.".format(args.port))
    if ip:
        print("En la consola del puente escribe:  ipad {}".format(ip))
    print("Sale una linea al cambiar los botones y dos por segundo con sticks y gatillos. Ctrl+C para salir.\n")

    last_seq = None
    last_accept = None
    latest = None
    shown_buttons = None
    senders = set()
    # Per window of one second.
    window_start = time.monotonic()
    count = lost = stale = invalid = 0
    max_gap = 0.0
    stats = "-"
    next_line = window_start + 0.5
    silent = False

    try:
        while True:
            try:
                data, addr = sock.recvfrom(64)
            except socket.timeout:
                data = None
            now = time.monotonic()

            if data is not None:
                p = decode(data)
                if p is None:
                    invalid += 1
                else:
                    if addr[0] not in senders:
                        senders.add(addr[0])
                        print("Paquetes desde {}".format(addr[0]))
                    resync = last_seq is None or now - last_accept >= RESYNC_S
                    if resync or seq_newer(p["seq"], last_seq):
                        if not resync:
                            lost += ((p["seq"] - last_seq) & 0xFFFF) - 1
                            max_gap = max(max_gap, now - last_accept)
                        last_seq, last_accept, latest = p["seq"], now, p
                        count += 1
                        silent = False
                        if args.log:
                            print(describe(p))
                        elif (p["buttons"], p["flags"]) != shown_buttons:
                            shown_buttons = (p["buttons"], p["flags"])
                            print("{} | {}".format(describe(p), stats))
                    else:
                        stale += 1

            if now - window_start >= 1.0:
                stats = "{} paq/s, hueco max {:.0f} ms, perdidos {}, viejos {}, invalidos {}".format(
                    count, max_gap * 1000, lost, stale, invalid)
                window_start = now
                count = 0
                max_gap = 0.0

            if not args.log and now >= next_line:
                next_line = now + 0.5
                if latest is not None and now - last_accept < 1.0:
                    print("{} | {}".format(describe(latest), stats))
                elif not silent:
                    print("Sin paquetes. Revisa la luz del control y el comando status del puente.")
                    silent = True
    except KeyboardInterrupt:
        print()


if __name__ == "__main__":
    main()
