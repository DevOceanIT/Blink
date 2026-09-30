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
static atomic_t desired_wink = ATOMIC_INIT(STATUS_LED_OFF);
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
	enum status_led_band wink = atomic_get(&desired_wink);
	unsigned int r = 0, g = 0, b = 0;

	if (!ready) {
		return;
	}
	/*
	 * A slower window is in a worse band than the steady colour can show,
	 * so borrow the light for two short pulses every 25 s. Swapping the
	 * colour rather than blanking it is what makes the wink informative:
	 * the pulse itself says "something crossed a line", and its colour says
	 * which line.
	 */
	if (wink != STATUS_LED_OFF &&
	    status_led_wink_on((unsigned int)k_uptime_get())) {
		band = wink;
	}
	/* Every channel that is on is driven at 100% duty, which on this
	 * inverted-polarity common-anode LED is as bright as the part goes.
	 * Orange is the one exception and has to be: green below full is what
	 * makes it orange instead of yellow. */
	switch (band) {
	case STATUS_LED_GREEN:  g = 100; break;
	case STATUS_LED_YELLOW: r = 100; g = 100; break;
	case STATUS_LED_ORANGE: r = 100; g = 25; break;
	case STATUS_LED_RED:    r = 100; break;
	case STATUS_LED_RED_FLASH:
		r = status_led_flash_on((unsigned int)(k_uptime_get() % 1000)) ? 100 : 0;
		break;
	/* Red and blue together, both full. The brightest purple the LED can
	 * make; there is no third primary to mix in. */
	case STATUS_LED_PURPLE: r = 100; b = 100; break;
	default: break;
	}
	int err = pulse(&red, r);

	err |= pulse(&green, g);
	err |= pulse(&blue, b);
	if (err && !reported_error)
		printk("[led] unable to set rear RGB channels\n");
	reported_error = err != 0;
	/* Only the blink needs waking again. Every other band, purple included,
	 * is static once written. 100 ms is well inside the 500 ms half-cycle. */
	/*
	 * Only animated states need waking again. The fast flash wants 100 ms,
	 * well inside its 500 ms half-cycle. The wink wants 40 ms because its
	 * pulses are 120 ms and both edges have to be caught -- it costs one
	 * PWM write per tick and only while a slow window is actually over a
	 * line, which is the minority of the time.
	 */
	if (atomic_get(&desired_band) == STATUS_LED_RED_FLASH)
		k_work_schedule(&animation, K_MSEC(100));
	else if (wink != STATUS_LED_OFF)
		k_work_schedule(&animation, K_MSEC(40));
}

static const char *band_name(enum status_led_band band)
{
	switch (band) {
	case STATUS_LED_OFF:       return "off";
	case STATUS_LED_GREEN:     return "green";
	case STATUS_LED_YELLOW:    return "yellow";
	case STATUS_LED_ORANGE:    return "orange";
	case STATUS_LED_RED:       return "red";
	case STATUS_LED_RED_FLASH: return "red-flash";
	case STATUS_LED_PURPLE:    return "purple";
	}
	return "?";
}

/* Serialize PWM writes on the system work queue. GUI updates change only the
 * requested band; repeated usage messages cannot restart the blink cycle. */
static void apply(enum status_led_band band, enum status_led_band wink)
{
	bool changed;

	if (!ready) {
		return;
	}
	changed = atomic_set(&desired_band, band) != band;
	changed |= atomic_set(&desired_wink, wink) != wink;
	if (changed) {
		/* On change only, so this cannot spam: the animations
		 * reschedule themselves without coming back through here.
		 * Worth having -- the LED is on the BACK of the board, so when
		 * a colour looks wrong this is the only way to tell a policy
		 * problem from a wiring one without turning the thing around. */
		if (wink == STATUS_LED_OFF) {
			printk("[led] band %s\n", band_name(band));
		} else {
			printk("[led] band %s, winking %s\n",
			       band_name(band), band_name(wink));
		}
		k_work_reschedule(&animation, K_NO_WAIT);
	}
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
	apply(STATUS_LED_OFF, STATUS_LED_OFF);
}

void status_led_update(double p1_session, double p1_weekly,
		       double p2_session, double p2_weekly, bool connected)
{
	enum status_led_band steady = status_led_steady_band(p1_session,
							     p2_session,
							     connected);
	enum status_led_band alert = status_led_band_for(p1_session, p1_weekly,
							 p2_session, p2_weekly,
							 connected);

	apply(steady, status_led_wink_band(steady, alert));
}
