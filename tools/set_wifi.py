#!/usr/bin/env python3
"""Store home WiFi credentials on the board over USB.

The SoftAP portal is the intended route and it is nicer when it works. This
exists because it does not always work: a blind radio, or simply no way to get
a phone onto the board's AP, leaves the setup screen with nothing to offer and
no keyboard to type on.

The password is read from the environment, never from the command line, so it
does not land in shell history or a process listing:

    launchctl bootout gui/$(id -u)/com.blink.bridge
    read -rs BLINK_WIFI_PSK && export BLINK_WIFI_PSK
    python3 tools/set_wifi.py --port /dev/cu.usbserial-10 --ssid 'My Network'
    unset BLINK_WIFI_PSK
    launchctl bootstrap gui/$(id -u) ~/Library/LaunchAgents/com.blink.bridge.plist

Omit BLINK_WIFI_PSK entirely for an open network. Nothing is printed except
whether the board accepted the write, and the board itself logs only
"[cfg] wifi credentials stored over USB".

The credentials are persisted, not joined: the board joins on its next boot,
which is what the portal flow does too (post-AP joins time out on this
hardware). Closing the port resets the board, so the reboot is automatic.
"""
import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from pc import protocol  # noqa: E402

PROTO_VERSION = 2


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Store WiFi credentials on the board over USB.")
    ap.add_argument("--port", required=True, help="board serial device")
    ap.add_argument("--ssid", required=True, help="home network name")
    ap.add_argument("--baud", type=int, default=115200)
    args = ap.parse_args()

    psk = os.environ.get("BLINK_WIFI_PSK", "")
    if len(args.ssid) > 32:
        print("SSID is longer than 32 characters.", file=sys.stderr)
        return 2
    if psk and not 8 <= len(psk) <= 63:
        # WPA2 requires 8..63 for a passphrase. Catch it here rather than
        # storing something the board can only fail to join with.
        print("WPA passphrase must be 8-63 characters.", file=sys.stderr)
        return 2

    import serial

    ser = serial.Serial(args.port, args.baud, timeout=0.2, write_timeout=2)
    try:
        # Opening the port resets this board and it takes ~30 s to come up
        # through its splash. Writing into a booting board is silently lost.
        # Opening the port resets this board and it takes ~30 s to come up
        # through its splash. Writing into a booting board is silently lost.
        #
        # Wait for `hello` or a `ping` when we can get one, but do NOT require
        # it: a board already sitting in the setup portal (exactly the state
        # you run this from) never sends either, and an earlier version of this
        # script refused to send anything at all in that case. The proto reader
        # thread is running regardless of which screen is up, so once the board
        # has had time to boot the command lands either way.
        print("waiting for the board...")
        deadline = time.time() + 40
        buf = b""
        seen = False
        while time.time() < deadline:
            buf += ser.read(512)
            if b'"hello"' in buf or b'"ping"' in buf:
                seen = True
                break
        if seen:
            print("board announced itself")
            time.sleep(2.0)
        else:
            # No announcement: give the boot the rest of its time, then send
            # blind. The reply is still the thing we judge by.
            remaining = 35 - (time.time() - (deadline - 40))
            if remaining > 0:
                time.sleep(remaining)
            print("no announcement (setup mode?); sending anyway")

        msg = {"t": "wifi_set", "v": PROTO_VERSION, "ssid": args.ssid}
        if psk:
            msg["psk"] = psk
        wire, why = protocol.encode_checked(msg)
        del psk, msg
        if why:
            print("Could not encode the request: %s" % why, file=sys.stderr)
            return 1
        ser.write(wire)
        ser.flush()
        del wire

        deadline = time.time() + 15
        buf = b""
        while time.time() < deadline:
            buf += ser.read(256)
            if b'"wifi_saved"' in buf:
                for line in buf.split(b"\n"):
                    if b'"wifi_saved"' in line:
                        try:
                            ok = json.loads(line.decode()).get("ok") is True
                        except ValueError:
                            ok = False
                        if ok:
                            # No settle delay needed: cfg persist() is a
                            # synchronous flash_area_erase + flash_area_write
                            # with no buffering, and the A/B slot scheme means
                            # a reset mid-write leaves the previous record
                            # intact rather than a torn one. An earlier version
                            # of this script slept here; it was treating a
                            # durability problem that does not exist.
                            print("Board stored the credentials. It will join "
                                  "on its next boot.")
                            return 0
                        print("Board refused the credentials.", file=sys.stderr)
                        return 1
        print("No reply from the board.", file=sys.stderr)
        return 1
    finally:
        ser.close()


if __name__ == "__main__":
    sys.exit(main())
