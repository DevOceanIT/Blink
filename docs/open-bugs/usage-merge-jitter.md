# The usage percentage jitters, and sometimes goes backwards

Reported 2026-10-01: "the number just went from 73 to 72, which I've never
seen." A cumulative percentage inside a window can only rise until the window
rolls over, so a decrease is an invariant violation, not a cosmetic wobble.

This is the measurement, so nobody re-derives it.

## Status, 2026-10-02

Fixed in the daemon, no firmware change:

- **The Codex variant** (below). `codex_cli.parse_cli_event` now runs each
  window through `base.rolled_over` -- the Claude status line's rule, moved
  there -- so a reading whose `resets_at` has passed reads 0% while fresh and
  unknown once stale, before any recency contest. The panel now corrects
  itself at the boundary without anyone opening Codex.
- **Mechanism 2**, the CLI frame dated by its renderer. Fixed by a new frame
  field, `reading_at`, that the merge ranks by: the first time that exact
  `rate_limits` block was seen. `observed_at`, and so the `age_s` on the
  wire, is deliberately left as the file's mtime -- the Wi-Fi feed draws the
  panel stale at `age_s >= 120` (`firmware/src/main.c`, NEV_FEED), so dating
  the wire age by the numbers would have made an in-use Claude Code flap
  amber whenever its percentage held still for two minutes.
- **Mechanism 1**, no hysteresis. `_pick` now lets a fresh older reading up
  to `HOLD_PCT` (one point) above the newest keep the dial. A reset, being
  tens of points, still wins on recency.

`tests/pc/test_claude_cli.py::test_the_reported_trace_no_longer_goes_backwards`
replays the trace below through the provider and the normalizer: the old code
draws 39, 40, 39, 41 and the fixed code 39, 40, 40, 41.

**Still open**, everything under "Other findings" below, and in particular:
the weekly `73 -> 68 -> 76` swing (an eight-point spread, past any one-point
hold; the Codex weekly spread of 60-62 across rollouts is the measured
instance), and the log line that names only the session source, which is
still the first thing to fix before chasing the weekly one.

## What was observed

From the daemon's own log, in order, one source name per line:

```
claude/cli      39.0%
claude/cli      39.0%
claude/desktop  40.0%
claude/desktop  40.0%
claude/desktop  40.0%
claude/cli      39.0%     <-- backwards
claude/cli      40.0%
claude/cli      41.0%
```

Also seen across a session rollover at 13:50: weekly moved 73 -> 68 -> 76,
which is far larger than the one-point step and may be a separate mechanism.

**Caveat on this evidence.** `pc/logbook.py:393` prints one `provider/src` pair
followed by BOTH percentages, and `src` is only ever the SESSION source
(`normalizer.py:175, 193`). So the trace above does not actually establish that
the *weekly* winner flipped. The weekly's source is never logged. Anyone
reproducing this should fix the log line first or they will mis-attribute it.

## Two mechanisms, not one

**1. No hysteresis, no per-source authority, no monotonic floor.**
`_pick` (`pc/normalizer.py:118`) ranks strictly by `observed_at`. The two
sources genuinely disagree: the desktop cache sees claude.ai and phone usage
the CLI cannot. So the winner alternates and the merged percentage alternates
with it. `firmware/src/usage_view.c:439` rounds with `(int32_t)(pct + 0.5)`, so
a disagreement of 0.2 straddling the .5 boundary renders as a full point. Any
hysteresis has to live on the daemon side; the firmware cannot smooth what it
is handed.

**2. The CLI frame is dated by the renderer, not by the reading.**
`pc/providers/claude_cli.py:59` takes `observed_at` from `statusline.json`'s
mtime, and `tools/blink-statusline.sh:50` rewrites that file on EVERY
status-line render whether or not `rate_limits` changed. The `rate_limits`
block inside holds whatever Claude Code last fetched, on its own slower
schedule. So the frame's recency advances while its number stays frozen, and a
fresh file holding an old number systematically beats an honestly-dated newer
desktop sample.

This is why the cli readings in the trace trail the desktop ones and then catch
up. It also contradicts the contract at `pc/providers/base.py:79`:
`observed_at` is "when the UNDERLYING DATA was written, not when we read it".
The comment at `claude_cli.py:62-65` asserts mtime equals reading age, which is
true of the file and false of the numbers in it.

## Other findings in the same merge path

- **`normalizer.py:197`** — `stale`, `observed_at`/wire `age_s` and `src`
  describe only the session source, while `weekly_pct` may come from a much
  older frame and is drawn as equally fresh. The on-screen age can therefore be
  honest about the session and silently wrong about the weekly. Pinned as
  intended behaviour by `test_each_window_resolves_independently:84`.
- **`normalizer.py:135`** — reset times are picked with no `_survives_rollover`
  check and no tie to the frame that supplied the percentage, so one window's
  percentage can be paired with the next window's countdown. With `resets_at`
  absent, `_window_has_reset` never fires and `_rolled_over` never zeroes that
  percentage.
- **`statusline_source.py:179`** — `observed_at = mtime_epoch` with no
  plausibility range check. Every sibling reader guards this
  (`claude_desktop.py:44-53`, `desktop_local_storage._valid:76`,
  `weekly_anchor.load:85`), and one of their comments names the failure: a
  future timestamp "is never stale AND beats every real reading, forever,
  pinning the panel". An NTP step, a restored backup or a wrong clock freezes
  the display on that payload while it captions itself as current.
- **`statusline_source.py:60`** — no upper bound on `used_percentage`, no range
  on `resets_at`, and `isinstance(resets, (int, float))` admits `True`. A
  `resets_at` in milliseconds yields a ~56,000-year countdown and permanently
  suppresses the burn rate; `{"resets_at": true}` fires a spurious rollover.
- **`normalizer.py:85`** — `_survives_rollover`'s second disjunct tests only
  that the attribute is non-None, not that it is the rollover being tested, so
  a merged frame is permanently exempt from every future rollover. Latent:
  `ingest.py:288` is the only production caller and does not nest. One line:
  the test needs `>= rolled_at`, not `is not None`.
- **`normalizer.py:115`** — `_rolled_at` only knows rollovers a source
  explicitly reported. The drop evidence the codebase already computes
  (`claude_desktop.session_burn_pph:257`, `weekly_anchor.refuted_by:178`, both
  of which read a drop as a rollover) is never consumed by `merge()`, so a
  pre-reset percentage can survive a rollover indefinitely.
- **`claude_desktop.py:370`** — the burn rate hard-codes `doc.get("samples")`
  while the percentage path goes through `_samples_array_by_shape`, which also
  accepts `history`, `usage` and a bare list. A layout change silently kills
  the rate while the percentages keep working.
- **`claude_desktop.py:121`** — `_newest_sample` takes both percentages from
  the single newest sample and keeps it when EITHER is usable, so one bad field
  drops the other window from this source entirely — another contributor to the
  flip-flop, invisible in the log because `src` names the session source.
- **`normalizer.py:129`** — returning None when no percentage is available also
  discards `state`, the counts and `label`, and `ingest.poll:290` then sends
  nothing at all. On a fresh install with no `rate_limits` yet, the state light
  never lights despite a running session.

## The Codex variant, measured 2026-10-01 14:01

The same defect on the other provider, and the clearest instance of it, because
here the stale reading carries its own proof of being expired.

`codex_cli.recent_rollouts` globs `~/.codex/sessions/*/*/*/rollout-*.jsonl` and
takes the most recently MODIFIED files. Each file's tail remembers whatever
usage was current when that session last wrote. With several sessions open they
disagree, and the winner is decided by file mtime.

Sampled four rollouts at one instant:

```
age     6s    window=300    used=4.0    resets=+298 min
age    41s    window=300    used=3.0    resets=+298 min
age   982s    window=300    used=98.0   resets=-4 min
age  1053s    window=300    used=91.0   resets=-4 min

age     6s    window=10080  used=62.0   resets=+3092 min
age  1053s    window=10080  used=60.0   resets=+3092 min
```

Two readings of the SAME five-hour window, four minutes after it rolled over:
the fresh files report the new window at 3-4%, the older ones still report 98%
and 91% of the window that just ended -- and they say so, with `resets_at` four
minutes in the PAST.

The owner's reported "my display says 98" is literally the `used=98.0` above.

**Codex usage only refreshes when a Codex session writes.** There is no
independent poller for it: the figure comes from the tail of a rollout file, so
it moves when, and only when, somebody uses Codex. The owner predicted this and
then proved it deliberately -- the LED went `purple` -> `green, winking red` ->
`purple` -> `yellow, winking red` across 13:58..14:01 *because he resumed a
Codex session*, which wrote the fresh 3-4% rollouts above. Left alone it would
have sat on 98% until the next time he used Codex, which could be days.

An earlier draft of this note claimed the panel corrected itself unprompted.
It did not. Do not use the LED transitions above as evidence of self-healing.

That is what makes the negative `resets_at` matter so much. The stale reading
carries unambiguous proof that it describes a dead window, so the daemon has
everything it needs to discount it WITHOUT a fresh sample -- and does not. Any
candidate whose `resets_at` is already past should be excluded or zeroed before
the recency contest, not after it. Same hole as the `_survives_rollover`
finding at `normalizer.py:135`, reached by a different road.

Fixing that would make the panel self-correcting at a window boundary for a
provider nobody is currently using, which is the whole point of showing a
countdown next to the number.

Note also the weekly spread in that sample: 60 to 62 across files, which is the
8-point swing behind the observed `73 -> 68 -> 76`.

## Smallest fix for the reported symptom

Two changes, and only these two are needed to stop the number going backwards:

1. Date the CLI frame by when the reading was taken rather than when the file
   was rewritten.
2. Add hysteresis in `_pick` so a sub-point disagreement cannot flip the
   winner.

Neither changes what a board reports on average. Everything else above is
real but is not this bug.
