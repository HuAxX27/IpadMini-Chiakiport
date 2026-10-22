#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
"""Fake bridge: sends contract C4 packets with a fixed pattern, no ESP32 needed.

Presses each button for half a second in turn, turns the left stick in a
circle and ramps the triggers, so a receiver (ck_pad_recv.py, or the iPad app
once it has its UDP receiver) can be tested without the board or the pad.

    python3 ck_pad_send.py 192.168.1.50
    python3 ck_pad_send.py 127.0.0.1 --seconds 5
"""

import argparse
import math
import socket
import struct
import time

PORT = 9400
PACKET = struct.Struct("<2sBBHIBBhhhh")
TOUCHPAD_BIT = 14
FLAG_TOUCHPAD = 0x01


def pattern(t):
    bit = int(t / 0.5) % 16
    buttons = 1 << bit
    flags = FLAG_TOUCHPAD if bit == TOUCHPAD_BIT else 0
    angle = t * math.pi  # one turn every two seconds
    lx = int(32767 * math.cos(angle))
    ly = int(32767 * math.sin(angle))
    ramp = int(255 * ((t % 2.0) / 2.0))
    return flags, buttons, ramp, 255 - ramp, lx, ly, 0, 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("host", help="receiver address, e.g. the iPad's IP")
    parser.add_argument("--port", type=int, default=PORT, help="UDP port (9400)")
    parser.add_argument("--rate", type=float, default=125.0, help="packets per second (125)")
    parser.add_argument("--seconds", type=float, default=0, help="stop after this long (0: never)")
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    period = 1.0 / args.rate
    start = time.monotonic()
    sent = 0
    print("Mandando a {}:{} a {:.0f} paquetes por segundo. Ctrl+C para salir.".format(args.host, args.port, args.rate))
    try:
        while True:
            t = time.monotonic() - start
            if args.seconds and t >= args.seconds:
                break
            flags, buttons, l2, r2, lx, ly, rx, ry = pattern(t)
            seq = sent & 0xFFFF
            sock.sendto(PACKET.pack(b"CK", 1, flags, seq, buttons, l2, r2, lx, ly, rx, ry), (args.host, args.port))
            sent += 1
            delay = start + sent * period - time.monotonic()
            if delay > 0:
                time.sleep(delay)
    except KeyboardInterrupt:
        pass
    print("Enviados {} paquetes.".format(sent))


if __name__ == "__main__":
    main()
