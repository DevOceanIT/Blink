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

	/*
	 * The steady colour must ignore the weekly entirely. This is the whole
	 * point of the 2026-09-30 change: a weekly stuck at 95% must not paint
	 * the light red for days while the session is idle.
	 */
	check(status_led_steady_band(10, 10, true), STATUS_LED_GREEN,
	      "steady ignores a bad weekly");
	check(status_led_band_for(10, 95, 10, 10, true), STATUS_LED_RED_FLASH,
	      "alert still sees that weekly");
	check(status_led_steady_band(75, 10, true), STATUS_LED_ORANGE,
	      "steady follows the worse session");
	check(status_led_steady_band(-1, -1, true), STATUS_LED_OFF,
	      "steady off with no session reading");
	check(status_led_steady_band(50, 50, false), STATUS_LED_OFF,
	      "steady off when disconnected");

	/* The wink carries only what the steady colour cannot say. */
	check(status_led_wink_band(STATUS_LED_GREEN, STATUS_LED_ORANGE),
	      STATUS_LED_ORANGE, "wink shows a worse slow window");
	check(status_led_wink_band(STATUS_LED_ORANGE, STATUS_LED_ORANGE),
	      STATUS_LED_OFF, "no wink when it adds nothing");
	check(status_led_wink_band(STATUS_LED_ORANGE, STATUS_LED_GREEN),
	      STATUS_LED_OFF, "no wink for a better window");
	check(status_led_wink_band(STATUS_LED_RED_FLASH, STATUS_LED_PURPLE),
	      STATUS_LED_OFF, "no wink over the urgent flash");
	check(status_led_wink_band(STATUS_LED_PURPLE, STATUS_LED_PURPLE),
	      STATUS_LED_OFF, "no wink over exhausted");
	check(status_led_wink_band(STATUS_LED_OFF, STATUS_LED_RED),
	      STATUS_LED_OFF, "no wink while disconnected");

	/* Two 120 ms pulses, then quiet until the period comes round. */
	assert(status_led_wink_on(0));
	assert(status_led_wink_on(119));
	assert(!status_led_wink_on(120));
	assert(!status_led_wink_on(239));
	assert(status_led_wink_on(240));
	assert(status_led_wink_on(359));
	assert(!status_led_wink_on(360));
	assert(!status_led_wink_on(12000));
	assert(!status_led_wink_on(24999));
	assert(status_led_wink_on(25000));	/* next period */
	if (failures == 0) {
		puts("status_led: 25 policy and 17 timing checks passed");
	}
	return failures ? 1 : 0;
}
