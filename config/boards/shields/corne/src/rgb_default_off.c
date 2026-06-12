/*
 * Keep Corne RGB underglow off after boot to save battery.
 *
 * ZMK can restore a previously-saved RGB "on" state from settings, and
 * CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_USB can also turn RGB on at boot when USB
 * powered. This delayed work runs after settings have loaded and forces the
 * local half back off. Layer color changes still update the saved HSB color, but
 * they do not turn RGB on by themselves.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/rgb_underglow.h>

static void rgb_default_off_work_handler(struct k_work *work) {
    int err = zmk_rgb_underglow_off();
    if (err < 0) {
        LOG_DBG("Failed to force RGB off after boot: %d", err);
    }
}

static K_WORK_DELAYABLE_DEFINE(rgb_default_off_work, rgb_default_off_work_handler);

static int rgb_default_off_init(void) {
    k_work_schedule(&rgb_default_off_work, K_SECONDS(2));
    return 0;
}

SYS_INIT(rgb_default_off_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
