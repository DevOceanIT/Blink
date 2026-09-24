#ifndef STATUS_LED_H
#define STATUS_LED_H

#include <stdbool.h>

enum status_led_band {
	STATUS_LED_OFF,
	STATUS_LED_GREEN,
	STATUS_LED_YELLOW,
	STATUS_LED_ORANGE,
	STATUS_LED_RED,
	STATUS_LED_RED_PULSE,
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
	if (worst >= 92.0) {
		return STATUS_LED_RED_PULSE;
	}
	if (worst >= 85.0) {
		return STATUS_LED_RED;
	}
	if (worst >= 75.0) {
		return STATUS_LED_ORANGE;
	}
	if (worst >= 60.0) {
		return STATUS_LED_YELLOW;
	}
	return STATUS_LED_GREEN;
}

/* Smooth triangular pulse, 640 ms per cycle, never completely dark. */
static inline unsigned int status_led_pulse_percent(unsigned int phase_ms)
{
	unsigned int phase = phase_ms % 640U;
	unsigned int ramp = phase < 320U ? phase : 640U - phase;
	return 10U + 90U * ramp / 320U;
}

/* Rear CYD RGB LED: the most-used available window across both providers.
 * Negative percentages are unavailable; disconnected data turns it off. */
void status_led_init(void);
void status_led_update(double p1_session, double p1_weekly,
		       double p2_session, double p2_weekly, bool connected);
void status_led_off(void);

#endif /* STATUS_LED_H */
