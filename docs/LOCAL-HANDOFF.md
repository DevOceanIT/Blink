# Physical Blink display handoff

This branch customizes the upstream Blink firmware for an ESP32-2432S028R
with ILI9341 display and XPT2046 touch. It does not change the separate
macOS desktop or menu-bar application.

## Current behavior

- Claude and Codex share one screen with matching provider rows.
- Each row has a session usage rate, 5h and 7d percentages and bars, and
  separate reset fields. Missing reset times display `Reset --`.
- Session rates are percentage points per hour. A newly booted board needs
  at least three fresh observations spanning ten minutes before a local rate
  is available; an upstream rate can be used while it gathers observations.
- Swipe down opens Settings; swipe up or Back closes it. The old left arrow
  and replay shortcut are removed.
- The rear RGB LED follows the highest known percentage across both providers
  and both windows, on round tens: green below 60%, yellow from 60%, orange
  from 70%, solid red from 80%, flashing red from 90%, solid purple at 100%.
  The flash is a hard on/off blink on a 1 s cycle, not the old smooth ramp.
  Nothing animates at 100%: once quota is gone the light stops asking for
  attention and just states the fact. Every lit channel runs at full duty, so
  each colour is as bright as the part goes; orange is the one blend that
  needs green held down, or it would read as yellow.
- WiFi credentials can be set over USB with `tools/set_wifi.py`, as an
  alternative to the SoftAP portal when the portal cannot be reached. It reads
  the passphrase from `BLINK_WIFI_PSK` so it never lands in shell history, and
  the firmware's `wifi_set` handler neither echoes nor logs it. The board
  persists and joins on its next boot; it does not join in place.
- A failed join no longer erases the stored network. `run_local_feed` tries
  twice, then opens the portal with the credentials still on flash, so the next
  power cycle retries them. It used to call `cfg_clear_wifi()` after a single
  failure, which combined with the blind-radio quirk made the device close to
  impossible to get onto a network and looked exactly like a save that never
  worked. Only the portal clears credentials now, by overwriting them.
- `tools/led_walk.py` feeds the board synthetic percentages so the bands can be
  seen on demand rather than waited for. The board logs `[led] band <name>` on
  every change, which is the only way to check a colour without turning the
  board around.

## Hardware and preservation

This is an unfused ESP32-D0WD-V3 revision 3.1 with 4 MiB flash. The working
configuration uses `firmware/pilot.conf`, MCUboot and a confirmed signed
application. Do not burn eFuses or apply an edition stamp. The private signing
key and factory dump are outside this repository and must remain private.

Factory backup is in the enclosing workspace at
`device-backups/2026-09-24/factory-68094785d970-4MiB.bin`, 4,194,304 bytes.
SHA-256: `fbec42e3ab8c85094fe7a77bb5dfe4f45f112c8b2f7e174ba7b0730b525c00a5`.
The local build and flash helpers are in the enclosing `build-tools/` folder.
Follow `CLAUDE.md` and the firmware instructions before building or flashing.

## Usage sources and limitations

The upstream USB bridge reads local usage records. It does not generate model
requests. Claude Desktop's local history was observed updating about every
15 minutes on this Mac, while the bridge checks for changes every two seconds.
The faster Local Storage usage record expected by upstream was absent. The
native app's exact reset data is in memory and not in its percentage-history
file. Do not invent a countdown or extrapolate a reset from the sample time.
A naturally refreshed Claude Code status line can provide exact reset times.

## Verification and remaining checks

The earlier combined layout, touch alignment, down/up Settings gestures and
green LED were physically confirmed by the owner. The revised rate layout still
requires another physical check.

The LED bands were confirmed on hardware 2026-09-26 by driving the board with
`tools/led_walk.py` and reading its own `[led] band <name>` line back at each
step: 60 -> yellow, 70 -> orange, 80 -> red, 90 -> red-flash, 100 -> purple.
That verifies the firmware's choice of band, not the colour a human sees, so
if a band ever looks wrong the log tells you whether to suspect the policy or
the wiring.

Host checks cover rate sampling, quota drops, changed reset boundaries, stale
observations, and the LED band boundaries and blink duty
(`tests/status_led/host_test.c`). Firmware must also be built, flashed and
boot-verified; configuration and compilation alone are not evidence of live
success.

GitHub publication is requested after review. Do not include private backups,
keys, real session captures, or Claude settings in the published repository.
