"""The one thing this provider remembers, and why it may.

The status line is rewritten only when Claude Code renders. When the
five-hour window expires and nobody is rendering, the payload stops carrying
a percentage, the frame stops being a candidate for the session dial, and the
panel falls to whatever else is on the bus -- in the field, a Claude Desktop
sample 57 hours old, whose age was then drawn as "56h ago" over a machine
that had used Claude Code six hours earlier.
"""
import json

from pc import statusline_source as ss
from pc.providers.claude_cli import ClaudeCliProvider

NOW = 1_787_700_000.0


def write(path, payload, mtime):
    path.write_text(json.dumps(payload), encoding="utf-8")
    import os
    os.utime(path, (mtime, mtime))
    return path


def payload(five_hour=27.0, resets_at=None, seven_day=12.0):
    limits = {"seven_day": {"used_percentage": seven_day}}
    if five_hour is not None:
        limits["five_hour"] = {"used_percentage": five_hour,
                               "resets_at": resets_at}
    return {"rate_limits": limits}


def test_a_live_reading_is_returned_alone(tmp_path):
    p = write(tmp_path / "statusline.json",
              payload(five_hour=27.0, resets_at=NOW + 900), NOW - 60)
    prov = ClaudeCliProvider(path=str(p))

    frames = prov.poll(NOW)

    assert len(frames) == 1
    assert frames[0].session_pct == 27.0


# The remembered payloads below carry NO five-hour reset stamp, which is what
# `payload()` writes by default and is the shape this memory is for. A stamp
# the clock has passed is proof the window ended, and a reading of a window
# that has ended is not a reading anyone should be offered -- see
# test_a_remembered_window_that_has_since_ended_is_not_re_offered below, and
# pc/statusline_source._rolled_over for the argument.


def test_the_last_reading_with_a_percentage_is_offered_when_the_file_loses_one(tmp_path):
    """The field case: the file was rewritten without the five-hour window,
    so the only session figure left in the world is the one we read six hours
    ago, and nothing says the window it describes has rolled."""
    p = tmp_path / "statusline.json"
    prov = ClaudeCliProvider(path=str(p))

    write(p, payload(five_hour=27.0), NOW - 6 * 3600)
    prov.poll(NOW - 6 * 3600 + 1)

    write(p, {"rate_limits": {"seven_day": {"used_percentage": 12.0}}}, NOW - 60)
    frames = prov.poll(NOW)

    remembered = [f for f in frames if f.session_pct >= 0]
    assert len(remembered) == 1
    assert remembered[0].session_pct == 27.0
    assert remembered[0].observed_at == NOW - 6 * 3600
    assert remembered[0].src == "cli"


def test_a_remembered_window_that_has_since_ended_is_not_re_offered(tmp_path):
    """The same memory, with the one fact that disqualifies it.

    The payload's five-hour window rolled two hours ago, and re-offering its
    27% would put usage that has already been forgiven on the dial -- the
    failure pc/normalizer's docstring says the whole design exists to
    prevent. What the memory still carries is the evidence: the epoch that
    window emptied, which is what stops a Claude Desktop sample taken before
    the same reset from inheriting the dial (pc/normalizer._survives_rollover).
    """
    p = tmp_path / "statusline.json"
    prov = ClaudeCliProvider(path=str(p))

    write(p, payload(five_hour=27.0, resets_at=NOW - 7200), NOW - 6 * 3600)
    prov.poll(NOW - 6 * 3600 + 1)

    write(p, {"rate_limits": {"seven_day": {"used_percentage": 12.0}}}, NOW - 60)
    frames = prov.poll(NOW)

    assert [f.session_pct for f in frames if f.session_pct >= 0] == []
    assert any(f.session_rolled_at == NOW - 7200 for f in frames)


def test_the_remembered_reading_is_marked_stale_by_its_own_age(tmp_path):
    """Not frozen at the staleness it had when captured. A six-hour-old
    number under a green dot is the confident-wrong-number failure
    pc/normalizer's docstring exists to prevent."""
    p = tmp_path / "statusline.json"
    prov = ClaudeCliProvider(path=str(p))

    write(p, payload(five_hour=27.0), NOW - 6 * 3600)
    prov.poll(NOW - 6 * 3600 + 1)
    assert prov.poll(NOW - 6 * 3600 + 1)[0].stale is False

    write(p, {"rate_limits": {}}, NOW - 60)
    remembered = [f for f in prov.poll(NOW) if f.session_pct >= 0][0]
    assert remembered.stale is True


def test_the_remembered_reading_ages(tmp_path):
    """The whole point: the age must answer "when did you last use Claude
    Code", so it grows with the wall clock instead of resetting each poll."""
    p = tmp_path / "statusline.json"
    prov = ClaudeCliProvider(path=str(p))

    write(p, payload(five_hour=27.0), NOW - 3600)
    prov.poll(NOW - 3600 + 1)
    write(p, {"rate_limits": {}}, NOW - 60)

    a = [f for f in prov.poll(NOW) if f.session_pct >= 0][0]
    b = [f for f in prov.poll(NOW + 60) if f.session_pct >= 0][0]
    assert a.observed_at == b.observed_at == NOW - 3600


def test_a_newer_reading_with_a_percentage_replaces_the_remembered_one(tmp_path):
    p = tmp_path / "statusline.json"
    prov = ClaudeCliProvider(path=str(p))

    write(p, payload(five_hour=27.0, resets_at=NOW + 900), NOW - 3600)
    prov.poll(NOW - 3599)
    write(p, payload(five_hour=61.0, resets_at=NOW + 900), NOW - 60)
    prov.poll(NOW)

    write(p, {"rate_limits": {}}, NOW - 30)
    remembered = [f for f in prov.poll(NOW) if f.session_pct >= 0][0]
    assert remembered.session_pct == 61.0


def test_a_vanished_file_still_leaves_the_memory(tmp_path):
    """read_payload returns nothing for an absent or malformed file, and that
    is not evidence the last reading never happened."""
    p = tmp_path / "statusline.json"
    prov = ClaudeCliProvider(path=str(p))

    write(p, payload(five_hour=27.0, resets_at=NOW + 900), NOW - 3600)
    prov.poll(NOW - 3599)
    p.unlink()

    frames = prov.poll(NOW)
    assert len(frames) == 1
    assert frames[0].session_pct == 27.0


def test_nothing_is_invented_before_the_first_good_reading(tmp_path):
    p = write(tmp_path / "statusline.json", {"rate_limits": {}}, NOW - 60)
    prov = ClaudeCliProvider(path=str(p))

    frames = prov.poll(NOW)
    assert len(frames) == 1  # the all() below is vacuously true over an empty list
    assert all(f.session_pct < 0 for f in frames)


def test_the_memory_does_not_survive_a_new_daemon(tmp_path):
    """Deliberately in-memory: a fresh process starts with no history. Pinned
    so the decision is visible rather than accidental."""
    p = tmp_path / "statusline.json"
    write(p, payload(five_hour=27.0, resets_at=NOW + 900), NOW - 3600)
    ClaudeCliProvider(path=str(p)).poll(NOW - 3599)

    write(p, {"rate_limits": {}}, NOW - 60)
    frames = ClaudeCliProvider(path=str(p)).poll(NOW)
    assert len(frames) == 1  # the all() below is vacuously true over an empty list
    assert all(f.session_pct < 0 for f in frames)


# --- dating a reading by its numbers, not its renderer ----------------------
#
# Claude Code rewrites statusline.json on every render; the `rate_limits`
# block inside changes only when it fetches. Ranking by the file's mtime let
# a re-rendered old number beat a newer Claude Desktop sample, and the panel
# stepped 40 -> 39 -> 40 (docs/open-bugs/usage-merge-jitter.md).


def test_a_re_render_keeps_the_time_the_numbers_first_appeared(tmp_path):
    p = tmp_path / "statusline.json"
    prov = ClaudeCliProvider(path=str(p))

    write(p, payload(five_hour=39.0), NOW - 600)
    prov.poll(NOW - 599)
    write(p, payload(five_hour=39.0), NOW - 5)      # same numbers, re-render
    frame, = prov.poll(NOW)

    assert frame.reading_at == NOW - 600
    assert frame.observed_at == NOW - 5     # the age on the wire is unchanged
    assert frame.stale is False             # and so is staleness


def test_new_numbers_are_dated_by_the_render_that_brought_them(tmp_path):
    p = tmp_path / "statusline.json"
    prov = ClaudeCliProvider(path=str(p))

    write(p, payload(five_hour=39.0), NOW - 600)
    prov.poll(NOW - 599)
    write(p, payload(five_hour=40.0), NOW - 5)
    frame, = prov.poll(NOW)

    assert frame.reading_at == NOW - 5


def test_a_terminal_re_rendering_an_older_fetch_is_not_made_new(tmp_path):
    """Two terminals, each drawing what IT last fetched. The file alternates
    between them; the older block must keep its older date every time."""
    p = tmp_path / "statusline.json"
    prov = ClaudeCliProvider(path=str(p))

    write(p, payload(five_hour=39.0), NOW - 600)
    prov.poll(NOW - 599)
    write(p, payload(five_hour=40.0), NOW - 300)
    prov.poll(NOW - 299)
    write(p, payload(five_hour=39.0), NOW - 5)      # the first terminal again
    frame, = prov.poll(NOW)

    assert frame.reading_at == NOW - 600


def test_a_file_that_went_back_in_time_is_dated_afresh(tmp_path):
    """A restored copy or a clock step: an earlier sighting at a LATER time
    is not evidence about this one."""
    p = tmp_path / "statusline.json"
    prov = ClaudeCliProvider(path=str(p))

    write(p, payload(five_hour=39.0), NOW - 60)
    prov.poll(NOW - 59)
    write(p, payload(five_hour=39.0), NOW - 600)
    frame, = prov.poll(NOW)

    assert frame.reading_at == NOW - 600


def test_the_memory_of_first_sightings_is_bounded(tmp_path):
    from pc.providers import claude_cli

    p = tmp_path / "statusline.json"
    prov = ClaudeCliProvider(path=str(p))
    for i in range(claude_cli.READINGS_KEPT * 3):
        write(p, payload(five_hour=float(i)), NOW - 1000 + i)
        prov.poll(NOW - 1000 + i)

    assert len(prov._first_seen) == claude_cli.READINGS_KEPT


def test_the_reported_trace_no_longer_goes_backwards(tmp_path):
    """End to end, through the normalizer: the field trace, replayed."""
    from pc import normalizer
    from pc.providers import base

    p = tmp_path / "statusline.json"
    prov = ClaudeCliProvider(path=str(p))

    def desk(at, pct):
        return base.NormalizedUsageFrame(
            provider="claude", src="desktop", observed_at=at,
            session_pct=pct)

    write(p, payload(five_hour=39.0), NOW - 600)
    shown = [normalizer.merge(prov.poll(NOW - 590)).session_pct]
    sample = desk(NOW - 300, 40.0)                  # desktop fetches 40
    shown.append(normalizer.merge(prov.poll(NOW - 290) + [sample]).session_pct)
    write(p, payload(five_hour=39.0), NOW - 120)    # cli re-renders its 39
    shown.append(normalizer.merge(prov.poll(NOW - 110) + [sample]).session_pct)
    write(p, payload(five_hour=41.0), NOW - 10)     # cli fetches again
    shown.append(normalizer.merge(prov.poll(NOW) + [sample]).session_pct)

    assert shown == [39.0, 40.0, 40.0, 41.0]
    assert shown == sorted(shown)
