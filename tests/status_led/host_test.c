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
	check(status_led_band_for(20, 69.9, -1, -1, true), STATUS_LED_YELLOW,
	      "weekly can lead");
	check(status_led_band_for(20, 20, 70, 20, true), STATUS_LED_ORANGE,
	      "second provider can lead");
	check(status_led_band_for(20, 20, 20, 80, true), STATUS_LED_RED,
	      "second weekly can lead");
	check(status_led_band_for(NAN, -1, 20, -1, true), STATUS_LED_GREEN,
	      "invalid number ignored");

	/* Every boundary and the value just under it, because the whole point of
	 * the 2026-09-26 change is that the bands land on round tens. */
	check(status_led_band_for(69.9, 0, 0, 0, true), STATUS_LED_YELLOW,
	      "yellow upper edge");
	check(status_led_band_for(70, 0, 0, 0, true), STATUS_LED_ORANGE,
	      "orange boundary");
	check(status_led_band_for(79.9, 0, 0, 0, true), STATUS_LED_ORANGE,
	      "orange upper edge");
	check(status_led_band_for(80, 0, 0, 0, true), STATUS_LED_RED,
	      "red boundary");
	check(status_led_band_for(89.9, 0, 0, 0, true), STATUS_LED_RED,
	      "solid red upper edge");
	check(status_led_band_for(90, 0, 0, 0, true), STATUS_LED_RED_FLASH,
	      "flash boundary");
	check(status_led_band_for(0, 0, 99.9, 0, true), STATUS_LED_RED_FLASH,
	      "flash upper edge");
	check(status_led_band_for(100, 0, 0, 0, true), STATUS_LED_PURPLE,
	      "exhausted quota goes solid purple, not animated");

	/* Square blink: fully on for the first half of each second, fully off for
	 * the second. No intermediate value anywhere, unlike the ramp it replaced. */
	assert(status_led_flash_on(0));
	assert(status_led_flash_on(499));
	assert(!status_led_flash_on(500));
	assert(!status_led_flash_on(999));
	assert(status_led_flash_on(1000));
	assert(status_led_flash_on(2499));
	assert(!status_led_flash_on(2500));
	if (failures == 0) {
		puts("status_led: 14 policy and 7 blink checks passed");
	}
	return failures ? 1 : 0;
}
