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
  and both windows: green below 60%, yellow from 60%, orange from 75%, solid
  red from 85%, pulsing red from 92%. The pulse is smooth with a 640 ms cycle.

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
green LED were physically confirmed by the owner. The revised rate layout and
red pulse require another physical check. Host checks cover rate sampling,
quota drops, changed reset boundaries, stale observations, LED thresholds and
pulse brightness. Firmware must also be built, flashed and boot-verified;
configuration and compilation alone are not evidence of live success.

GitHub publication is requested after review. Do not include private backups,
keys, real session captures, or Claude settings in the published repository.
