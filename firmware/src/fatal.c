#include <zephyr/kernel.h>
#include <zephyr/fatal.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_REGISTER(fatal, LOG_LEVEL_ERR);

/*
 * Zephyr's default fatal handler halts the CPU. On a headless board that is
 * the right call -- somebody is watching a console. On this one it means the
 * screen keeps showing whatever pixels were up at the moment of the fault,
 * forever, with no clue anything is wrong. The display looks alive and is
 * dead, and the only cure is unplugging it.
 *
 * Observed twice: 2026-09-24 and 2026-09-25, both an LVGL allocation failing
 * while the settings panel was being built. LVGL's lv_obj_allocate_spec_attr()
 * returns without allocating when the pool is dry, its caller in
 * lv_obj_refresh_ext_draw_size() does not re-check, and the resulting NULL
 * store takes the whole board down.
 *
 * The pool has been enlarged so that should stop happening, but "should" is
 * doing a lot of work there: any unchecked allocation anywhere in LVGL fails
 * the same way. Rebooting turns the worst case from a dead display into a few
 * seconds of splash screen. Nothing this firmware shows is precious enough to
 * be worth staying dead to preserve.
 */

/*
 * Except at boot. If the fault is in early init, rebooting just does it again,
 * and a board that reboot-loops is harder to diagnose than one that stopped --
 * the console scrolls past before anyone can read it, and USB never enumerates
 * long enough to talk to it.
 *
 * So crashes in the first half-minute halt the way they always did, on the
 * assumption that a fault that early is reproducible and someone is watching.
 * Later ones recover, on the assumption that nobody is.
 */
#define FATAL_REBOOT_GRACE_MS	30000

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	ARG_UNUSED(esf);

	int64_t up = k_uptime_get();

	LOG_PANIC();

	if (up < FATAL_REBOOT_GRACE_MS) {
		LOG_ERR("fatal %u at %lld ms: too early to reboot, halting",
			reason, up);
		arch_system_halt(reason);
		CODE_UNREACHABLE;
	}

	LOG_ERR("fatal %u after %lld ms: rebooting", reason, up);
	sys_reboot(SYS_REBOOT_COLD);
	CODE_UNREACHABLE;
}
