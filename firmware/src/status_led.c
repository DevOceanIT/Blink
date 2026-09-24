/* The rear RGB LED is a quick warning for the least available quota.
 * It is separate from the TFT backlight, and its three channels are active
 * low on the ESP32-2432S028R (GPIO4 red, GPIO16 green, GPIO17 blue). */
#include <zephyr/drivers/pwm.h>
#include <zephyr/sys/printk.h>

#include "status_led.h"

static const struct pwm_dt_spec red = PWM_DT_SPEC_GET(DT_NODELABEL(rear_red));
static const struct pwm_dt_spec green = PWM_DT_SPEC_GET(DT_NODELABEL(rear_green));
static const struct pwm_dt_spec blue = PWM_DT_SPEC_GET(DT_NODELABEL(rear_blue));
static bool ready;
static int last_band = -1;

static int pulse(const struct pwm_dt_spec *led, unsigned int percent)
{
	return pwm_set_pulse_dt(led, (uint32_t)((uint64_t)led->period * percent / 100U));
}

static void apply(enum status_led_band band)
{
	unsigned int r = 0, g = 0;

	if (!ready || band == last_band) {
		return;
	}
	switch (band) {
	case STATUS_LED_GREEN:  g = 100; break;
	case STATUS_LED_YELLOW: r = 100; g = 100; break;
	case STATUS_LED_ORANGE: r = 100; g = 25; break;
	case STATUS_LED_RED:    r = 100; break;
	default: break;
	}
	int err = pulse(&red, r);

	err |= pulse(&green, g);
	err |= pulse(&blue, 0);
	if (err) {
		printk("[led] unable to set rear RGB channels\n");
		return;
	}
	last_band = band;
}

void status_led_init(void)
{
	ready = pwm_is_ready_dt(&red) && pwm_is_ready_dt(&green) &&
		pwm_is_ready_dt(&blue);
	if (!ready) {
		printk("[led] rear RGB controller unavailable\n");
		return;
	}
	apply(STATUS_LED_OFF);
}

void status_led_off(void)
{
	apply(STATUS_LED_OFF);
}

void status_led_update(double p1_session, double p1_weekly,
		       double p2_session, double p2_weekly, bool connected)
{
	apply(status_led_band_for(p1_session, p1_weekly, p2_session,
				  p2_weekly, connected));
}
