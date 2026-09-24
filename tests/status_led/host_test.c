/* Run: cc -Wall -Werror -I firmware/src tests/status_led/host_test.c -o /tmp/ledtest && /tmp/ledtest */
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include "status_led.h"

static int failures;

static void check(enum status_led_band got, enum status_led_band want,
		  const char *label)
{
	if (got != want) {
		fprintf(stderr, "FAIL %s: got %d, want %d\n", label, got, want);
		failures++;
	}
}

int main(void)
{
	check(status_led_band_for(-1, -1, -1, -1, true), STATUS_LED_OFF,
	      "no reading");
	check(status_led_band_for(100, 20, 20, 20, false), STATUS_LED_OFF,
	      "disconnected");
	check(status_led_band_for(59.9, -1, -1, -1, true), STATUS_LED_GREEN,
	      "green upper edge");
	check(status_led_band_for(60, 20, -1, -1, true), STATUS_LED_YELLOW,
	      "yellow boundary");
	check(status_led_band_for(20, 74.9, -1, -1, true), STATUS_LED_YELLOW,
	      "weekly can lead");
	check(status_led_band_for(20, 20, 75, 20, true), STATUS_LED_ORANGE,
	      "second provider can lead");
	check(status_led_band_for(20, 20, 20, 85, true), STATUS_LED_RED,
	      "second weekly can lead");
	check(status_led_band_for(NAN, -1, 20, -1, true), STATUS_LED_GREEN,
	      "invalid number ignored");
	check(status_led_band_for(84.9, 0, 0, 0, true), STATUS_LED_ORANGE,
	      "orange upper edge");
	check(status_led_band_for(91.9, 0, 0, 0, true), STATUS_LED_RED,
	      "solid red upper edge");
	check(status_led_band_for(0, 0, 92, 0, true), STATUS_LED_RED_PULSE,
	      "pulse boundary");
	check(status_led_band_for(100, 0, 0, 0, true), STATUS_LED_RED_PULSE,
	      "exhausted quota pulses");
	assert(status_led_pulse_percent(0) == 10);
	assert(status_led_pulse_percent(160) == 55);
	assert(status_led_pulse_percent(320) == 100);
	assert(status_led_pulse_percent(480) == 55);
	assert(status_led_pulse_percent(640) == 10);
	if (failures == 0) {
		puts("status_led: 12 policy and 5 pulse checks passed");
	}
	return failures ? 1 : 0;
}
