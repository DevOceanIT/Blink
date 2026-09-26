#!/usr/bin/env python3
"""Walk the rear RGB LED through every band so a person can watch it.

The LED is on the BACK of the board and its colour depends on real quota, so
the only way to see the bands without waiting weeks is to feed the board
synthetic usage. This sends ordinary `usage` protocol messages -- nothing
special-cased in the firmware, no test hook -- so what you see is exactly the
policy that runs in production.

It talks to the board directly, so the bridge has to be out of the way. Stop it
first and start it again afterwards:

    launchctl bootout gui/$(id -u)/com.blink.bridge
    python3 tools/led_walk.py --port /dev/cu.usbserial-10
    launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.blink.bridge.plist

The board's serial device name moves depending on what else is plugged in;
~/.blink/board.json records the one the bridge last used.

Nothing is written to the board's config: usage messages are transient, so a
reboot puts the real numbers straight back.
"""
import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from pc import protocol  # noqa: E402

# (percent, what the firmware should choose). Kept in step with
# status_led_band_for() in firmware/src/status_led.h.
STEPS = (
    (30.0, "green"),
    (60.0, "yellow"),
    (70.0, "orange"),
    (80.0, "red"),
    (90.0, "red-flash"),
    (100.0, "purple"),
)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", required=True, help="board serial device")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--hold", type=float, default=6.0,
                    help="seconds to hold each band (default 6)")
    args = ap.parse_args()

    import serial

    ser = serial.Serial(args.port, args.baud, timeout=0.2, write_timeout=2)
    try:
        # Opening the port resets this board, and it takes a while to come up
        # through its splash. Sending into a booting board is how you get a
        # walk that silently does nothing.
        print("waiting for the board to finish booting...")
        deadline = time.time() + 45
        buf = b""
        while time.time() < deadline:
            buf += ser.read(512)
            if b'"hello"' in buf or b'"ping"' in buf:
                break
        print("board is up\n")

        welcome, why = protocol.encode_checked(
            protocol.welcome("led-walk", "0"))
        if why:
            print("could not encode handshake: %s" % why, file=sys.stderr)
            return 1
        ser.write(welcome)
        ser.flush()
        time.sleep(1.0)

        for pct, expect in STEPS:
            msg, why = protocol.encode_checked(
                protocol.usage(pct, 0, 0.0, 0, {}))
            if why:
                print("could not encode %g%%: %s" % (pct, why), file=sys.stderr)
                return 1
            ser.write(msg)
            ser.flush()
            print("%5.1f%%  -> expect %s" % (pct, expect))
            # Echo the board's own [led] line back, which is the actual proof.
            end = time.time() + args.hold
            while time.time() < end:
                chunk = ser.read(256)
                for line in chunk.split(b"\n"):
                    text = line.decode("utf-8", "replace").strip()
                    if "[led]" in text:
                        print("        board says: %s" % text)
        print("\nDone. The real numbers come back on the next reboot.")
        return 0
    finally:
        ser.close()


if __name__ == "__main__":
    sys.exit(main())
