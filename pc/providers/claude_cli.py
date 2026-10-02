"""Claude Code's status line, as a provider on the ingestion bus.

A thin adapter, on purpose. All of the reasoning about this source -- the
freshness bound and why age is the wrong primary signal, carrying a window
across its own reset, why an absent window is -1 and not 0 -- lives in
pc/statusline_source and stays there. Re-implementing any of it here would
give the project two rules for the same file.

This briefly read one payload per session, to take the worst of several
context windows and name the model in use. Both came off the panel -- with
several agents running there are several contexts at different levels and no
single number is any of them -- and what is left is account-wide: every open
terminal reports the same two percentages, so the most recent render is as
good as any.

One thing IS remembered, and only one: the last payload whose five-hour
window we could actually read. The file is rewritten when Claude Code
renders, and when the window expires with nobody rendering, the rewrite
drops the percentage -- so the reading does not lose a comparison, it stops
existing, and the panel falls to whatever else is on the bus. In the field
that was a Claude Desktop sample 57 hours old, whose age was drawn over a
machine that had used Claude Code six hours earlier.

The RAW payload is kept, not the frame it maps to, and it is re-mapped at
the current clock every time it is offered. That is the whole reason this is
five lines instead of a policy: staleness, a window carried across its own
reset, and the refusal to zero an old one all live in map_statusline_frame
and all depend on `now`. A cached frame would freeze `stale=False` at
capture and put a green dot over an hours-old number, which is precisely
what pc/normalizer's docstring exists to prevent.

The memory dies with the process, deliberately. It answers "since this
daemon started, when did you last use Claude Code", and a daemon restart is
almost always an app update or a login -- after which the desktop cache is
as good an answer as we have. Persisting it would put a second copy of a
file that already exists on disk into ~/.blink, with its own invalidation
and corruption paths, for a case the field report does not contain.
"""
import dataclasses
import json

from pc import statusline_source as ss
from pc.providers import base

PROVIDER_ID = ss.PROVIDER_ID
SRC_ID = ss.SRC_ID

# How many distinct `rate_limits` blocks to remember the first sight of.
# Several terminals each re-render the reading THEY last fetched, so the file
# can alternate between two or three of them; remembering only the latest
# would re-date an old one as new every time its terminal rendered again. A
# window's percentage only rises, so a block is never legitimately seen again
# once it has been superseded, and a handful covers every terminal open.
READINGS_KEPT = 16


class ClaudeCliProvider(base.ProviderParser):
    def __init__(self, path=None):
        self._path = path if path is not None else ss.PAYLOAD_PATH
        # (payload, mtime, reading_at) for the last reading that had a
        # five-hour percentage, or None before the first one. See the module
        # docstring.
        self._remembered = None
        # {rate_limits as canonical JSON: the earliest mtime it was seen at},
        # oldest first. See _reading_at.
        self._first_seen = {}

    def get_provider_id(self) -> str:
        return PROVIDER_ID

    def path(self):
        return self._path

    def parse_cli_event(self, raw_payload, now_epoch, observed_at):
        """A push from Claude Code, already read.

        `observed_at` is the payload's mtime rather than now: the file is
        written when Claude Code renders, so its age is the reading's age.
        Passing now() here would make every reading look current, which is
        the one thing the freshness logic exists to prevent.
        """
        if not isinstance(raw_payload, dict):
            return None
        return ss.map_statusline_frame(raw_payload, now_epoch, observed_at)

    def _reading_at(self, payload, mtime):
        """When this payload's numbers first appeared, not when it was written.

        Claude Code rewrites the file on every render and refreshes the
        `rate_limits` inside on its own, slower schedule, so the mtime says
        when the panel was last drawn in a terminal and not when the numbers
        were taken. The first mtime this exact block was seen at is the
        closest honest answer there is: the numbers cannot be newer than
        their first appearance, and every later rewrite of the same block is
        a re-render, not a reading. See base.NormalizedUsageFrame.reading_at
        for the jitter this caused and why `observed_at` is left alone.

        Since the daemon started, like the rest of what this class remembers:
        the first poll after a restart dates a reading by its mtime, which is
        exactly what every poll did before this existed.
        """
        try:
            key = json.dumps(payload.get("rate_limits"), sort_keys=True)
        except (TypeError, ValueError):
            return mtime        # not JSON-shaped; no basis to say otherwise
        first = self._first_seen.get(key)
        if first is not None and first <= mtime:
            return first
        # New, or the file went BACKWARDS in time (a restored copy, a clock
        # step) -- then the earlier sighting is not evidence about this one.
        self._first_seen.pop(key, None)
        self._first_seen[key] = mtime
        while len(self._first_seen) > READINGS_KEPT:
            del self._first_seen[next(iter(self._first_seen))]
        return mtime

    def _frame(self, payload, now_epoch, mtime, reading_at):
        frame = self.parse_cli_event(payload, now_epoch, mtime)
        if frame is not None and reading_at != frame.reading_at:
            frame = dataclasses.replace(frame, reading_at=reading_at)
        return frame

    def poll(self, now_epoch):
        payload, mtime = ss.read_payload(self._path)
        frames = []
        if payload is not None:
            reading_at = (self._reading_at(payload, mtime)
                          if isinstance(payload, dict) else mtime)
            live = self._frame(payload, now_epoch, mtime, reading_at)
            if live is not None:
                frames.append(live)
                # base.UNKNOWN is -1.0; anything >= 0 is a real percentage,
                # including the hard 0.0 map_statusline_frame computes for a
                # window it watched roll over.
                if live.session_pct >= 0:
                    self._remembered = (payload, mtime, reading_at)
                    return frames

        # The live reading has no session figure, so offer the last one that
        # did -- ALONGSIDE the live frame rather than instead of it. They are
        # merged field by field, so the live payload keeps whatever it still
        # has (a seven-day percentage outlives the five-hour window every
        # time) and the remembered one is a candidate for the session dial
        # only. The normalizer then ranks it by recency like anything else,
        # so it wins the dial only when it is genuinely the freshest session
        # reading in the set.
        if self._remembered is not None:
            payload, mtime, reading_at = self._remembered
            remembered = self._frame(payload, now_epoch, mtime, reading_at)
            if remembered is not None:
                frames.append(remembered)
        return frames
