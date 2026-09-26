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

/* Pure policy so the boundaries, missing readings, and two-provider maximum
 * can be checked on a host without changing real usage or flashing a board. */
static inline enum status_led_band status_led_band_for(double p1_session,
			double p1_weekly, double p2_session,
			double p2_weekly, bool connected)
{
	const double readings[] = {p1_session, p1_weekly, p2_session, p2_weekly};
	double worst = -1.0;

	if (!connected) {
		return STATUS_LED_OFF;
	}
	for (unsigned int i = 0; i < 4; i++) {
		/* NaN fails >= and cannot become a falsely calm green. */
		if (readings[i] >= 0.0 && readings[i] > worst) {
			worst = readings[i];
		}
	}
	if (worst < 0.0) {
		return STATUS_LED_OFF;
	}
	/* Round tens, at Christopher's request 2026-09-26, replacing 60/75/85/92.
	 * Every band now falls on a number he can read off the screen and predict,
	 * and the two crowded top bands are spread out so the colours are actually
	 * distinguishable in use rather than collapsing straight to red.
	 *
	 * 100 is reachable exactly: pc/protocol.py clamps anything above 100 down
	 * to 100.0 before it reaches the wire, so purple is not unreachable the
	 * way a `> 100` test would make it. */
	if (worst >= 100.0) {
		return STATUS_LED_PURPLE;
	}
	if (worst >= 90.0) {
		return STATUS_LED_RED_FLASH;
	}
	if (worst >= 80.0) {
		return STATUS_LED_RED;
	}
	if (worst >= 70.0) {
		return STATUS_LED_ORANGE;
	}
	if (worst >= 60.0) {
		return STATUS_LED_YELLOW;
	}
	return STATUS_LED_GREEN;
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

/* Rear CYD RGB LED: the most-used available window across both providers.
 * Negative percentages are unavailable; disconnected data turns it off. */
void status_led_init(void);
void status_led_update(double p1_session, double p1_weekly,
		       double p2_session, double p2_weekly, bool connected);
void status_led_off(void);

#endif /* STATUS_LED_H */
