#ifndef STATUS_LED_H
#define STATUS_LED_H

#include <stdbool.h>

enum status_led_band {
	STATUS_LED_OFF,
	STATUS_LED_GREEN,
	STATUS_LED_YELLOW,
	STATUS_LED_ORANGE,
	STATUS_LED_RED,
	STATUS_LED_RED_FLASH,
	STATUS_LED_PURPLE,
};

/* Round tens, at Christopher's request 2026-09-26, replacing 60/75/85/92.
 * Every band falls on a number he can read off the screen and predict, and the
 * two crowded top bands are spread out so the colours are distinguishable in
 * use rather than collapsing straight to red.
 *
 * 100 is reachable exactly: pc/protocol.py clamps anything above 100 down to
 * 100.0 before it reaches the wire, so purple is not stranded behind a
 * threshold nothing can satisfy. A negative percentage is "not available". */
static inline enum status_led_band status_led_band_of(double pct)
{
	if (pct < 0.0) {		/* NaN lands here too: it fails every test */
		return STATUS_LED_OFF;
	}
	if (pct >= 100.0) {
		return STATUS_LED_PURPLE;
	}
	if (pct >= 90.0) {
		return STATUS_LED_RED_FLASH;
	}
	if (pct >= 80.0) {
		return STATUS_LED_RED;
	}
	if (pct >= 70.0) {
		return STATUS_LED_ORANGE;
	}
	if (pct >= 60.0) {
		return STATUS_LED_YELLOW;
	}
	return STATUS_LED_GREEN;
}

static inline double status_led_worst(const double *readings, unsigned int n)
{
	double worst = -1.0;

	for (unsigned int i = 0; i < n; i++) {
		/* NaN fails >= and cannot become a falsely calm green. */
		if (readings[i] >= 0.0 && readings[i] > worst) {
			worst = readings[i];
		}
	}
	return worst;
}

/*
 * Worst of all four windows. This was the whole policy until 2026-09-30; it is
 * now only the ALERT band -- see status_led_steady_band below for why.
 */
static inline enum status_led_band status_led_band_for(double p1_session,
			double p1_weekly, double p2_session,
			double p2_weekly, bool connected)
{
	const double readings[] = {p1_session, p1_weekly, p2_session, p2_weekly};

	if (!connected) {
		return STATUS_LED_OFF;
	}
	return status_led_band_of(status_led_worst(readings, 4));
}

/*
 * The STEADY colour: the five-hour session windows only, never the weekly.
 *
 * Worst-of-all-four made the light useless for days at a time. The weekly
 * window moves over a week, so the moment it crossed 60% the LED pinned at
 * yellow-or-worse and stayed there until the reset -- always on, never
 * changing, telling you nothing you did not already know, and unable to warn
 * you when something actually happened. Christopher, 2026-09-30: "I don't want
 * this thing showing me orange for two days until the weekly resets."
 *
 * The session window resets every few hours, so a colour derived from it
 * climbs while you work and falls back to green afterwards. That is a
 * timescale on which a light is worth glancing at.
 *
 * The weekly is not discarded; it speaks through the wink below.
 */
static inline enum status_led_band status_led_steady_band(double p1_session,
			double p2_session, bool connected)
{
	const double readings[] = {p1_session, p2_session};

	if (!connected) {
		return STATUS_LED_OFF;
	}
	return status_led_band_of(status_led_worst(readings, 2));
}

/*
 * Should the light wink, and if so in which colour? Returns the alert band
 * when a window the steady colour cannot see is in a worse band, else OFF.
 *
 * Deliberately silent in two cases. RED_FLASH is already flashing hard for an
 * urgent reason, and PURPLE means quota is actually gone -- neither wants a
 * second animation layered over it, and in both the steady colour is already
 * the most severe thing there is to say.
 */
static inline enum status_led_band status_led_wink_band(
			enum status_led_band steady, enum status_led_band alert)
{
	if (steady == STATUS_LED_RED_FLASH || steady == STATUS_LED_PURPLE) {
		return STATUS_LED_OFF;
	}
	if (steady == STATUS_LED_OFF || alert <= steady) {
		return STATUS_LED_OFF;
	}
	return alert;
}

/* Hard on/off blink, 1 s cycle, for the 90s band.
 *
 * This replaces a smooth 640 ms triangular ramp that never went fully dark.
 * That read as breathing -- ambient, easy to stop noticing -- and it ran at
 * the top band where it was on screen constantly and merely distracting. A
 * square blink is unambiguous, and it now sits below the exhausted band
 * rather than at it, so nothing animates once quota is actually gone. */
static inline bool status_led_flash_on(unsigned int phase_ms)
{
	return (phase_ms % 1000U) < 500U;
}

/*
 * The wink: two short pulses, then a long quiet gap.
 *
 * 25 s because Christopher asked for "twenty or thirty seconds -- subtle, but
 * if I have my gaze in that direction I'll be able to capture it". That is the
 * whole specification: it must be catchable by someone who happens to look,
 * and ignorable by someone who does not. Anything faster becomes the nagging
 * the steady colour was changed to avoid.
 *
 * Two pulses rather than one so it reads as deliberate. A single blink at this
 * spacing is indistinguishable from a glitch.
 */
#define STATUS_LED_WINK_PERIOD_MS	25000U

static inline bool status_led_wink_on(unsigned int phase_ms)
{
	unsigned int p = phase_ms % STATUS_LED_WINK_PERIOD_MS;

	return p < 120U || (p >= 240U && p < 360U);
}

/* Rear CYD RGB LED. The steady colour follows the session windows; a slower
 * window in a worse band winks over it. Negative percentages are unavailable;
 * disconnected data turns it off. */
void status_led_init(void);
void status_led_update(double p1_session, double p1_weekly,
		       double p2_session, double p2_weekly, bool connected);
void status_led_off(void);

#endif /* STATUS_LED_H */
