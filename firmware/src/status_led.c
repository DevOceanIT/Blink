/* The rear RGB LED is a quick warning for the least available quota.
 * It is separate from the TFT backlight, and its three channels are active
 * low on the ESP32-2432S028R (GPIO4 red, GPIO16 green, GPIO17 blue). */
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

#include "status_led.h"

static const struct pwm_dt_spec red = PWM_DT_SPEC_GET(DT_NODELABEL(rear_red));
static const struct pwm_dt_spec green = PWM_DT_SPEC_GET(DT_NODELABEL(rear_green));
static const struct pwm_dt_spec blue = PWM_DT_SPEC_GET(DT_NODELABEL(rear_blue));
static bool ready;
static atomic_t desired_band = ATOMIC_INIT(STATUS_LED_OFF);
static bool reported_error;
static void animate(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(animation, animate);

static int pulse(const struct pwm_dt_spec *led, unsigned int percent)
{
	return pwm_set_pulse_dt(led, (uint32_t)((uint64_t)led->period * percent / 100U));
}

static void animate(struct k_work *work)
{
	ARG_UNUSED(work);
	enum status_led_band band = atomic_get(&desired_band);
	unsigned int r = 0, g = 0;

	if (!ready) {
		return;
	}
	switch (band) {
	case STATUS_LED_GREEN:  g = 100; break;
	case STATUS_LED_YELLOW: r = 100; g = 100; break;
	case STATUS_LED_ORANGE: r = 100; g = 25; break;
	case STATUS_LED_RED:    r = 100; break;
	case STATUS_LED_RED_PULSE:
		r = status_led_pulse_percent((unsigned int)(k_uptime_get() % 640));
		break;
	default: break;
	}
	int err = pulse(&red, r);

	err |= pulse(&green, g);
	err |= pulse(&blue, 0);
	if (err && !reported_error)
		printk("[led] unable to set rear RGB channels\n");
	reported_error = err != 0;
	if (band == STATUS_LED_RED_PULSE)
		k_work_schedule(&animation, K_MSEC(20));
}

/* Serialize PWM writes on the system work queue. GUI updates change only the
 * requested band; repeated usage messages cannot restart the pulse cycle. */
static void apply(enum status_led_band band)
{
	if (ready && atomic_set(&desired_band, band) != band)
		k_work_reschedule(&animation, K_NO_WAIT);
}

void status_led_init(void)
{
	ready = pwm_is_ready_dt(&red) && pwm_is_ready_dt(&green) &&
		pwm_is_ready_dt(&blue);
	if (!ready) {
		printk("[led] rear RGB controller unavailable\n");
		return;
	}
	k_work_reschedule(&animation, K_NO_WAIT);
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
