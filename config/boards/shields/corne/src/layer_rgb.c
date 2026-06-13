/*
 * Layer-driven RGB underglow colors for Corne.
 *
 * This keeps RGB color tied to the currently active layer, but only while RGB is
 * explicitly on. It does not turn RGB on and does not force a particular effect.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/event_manager.h>
#include <dt-bindings/zmk/rgb.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/rgb_underglow.h>
#include <zmk/split/central.h>

struct layer_rgb_color {
    uint16_t h;
    uint8_t s;
    uint8_t b;
};

static const struct layer_rgb_color layer_colors[] = {
    {0, 0, 30},     /* BASE: white */
    {210, 100, 30}, /* 123: blue */
    {280, 100, 30}, /* {}: purple */
    {120, 100, 30}, /* PTR: green */
    {15, 100, 30},  /* CFG: red-orange */
};

static void invoke_peripheral_rgb(uint8_t source, uint32_t command, uint32_t value) {
    struct zmk_behavior_binding binding = {
        .behavior_dev = "rgb_ug",
        .param1 = command,
        .param2 = value,
    };

    struct zmk_behavior_binding_event event = {
        .layer = zmk_keymap_highest_layer_active(),
        .position = 0,
        .timestamp = k_uptime_get(),
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = 0,
#endif
    };

    int err = zmk_split_central_invoke_behavior(source, &binding, event, true);
    if (err < 0) {
        LOG_DBG("Failed to sync RGB command %u to peripheral %u: %d", command, source, err);
    }
}

static void sync_peripherals_rgb(const struct layer_rgb_color color) {
    for (uint8_t source = 0; source < ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT; source++) {
        invoke_peripheral_rgb(source, RGB_COLOR_HSB_CMD,
                              RGB_COLOR_HSB_VAL(color.h, color.s, color.b));
    }
}

static int layer_rgb_listener(const zmk_event_t *eh) {
    bool rgb_on;
    int err = zmk_rgb_underglow_get_state(&rgb_on);

    if (err < 0 || !rgb_on) {
        return err;
    }

    uint8_t layer = zmk_keymap_highest_layer_active();

    if (layer >= ARRAY_SIZE(layer_colors)) {
        layer = 0;
    }

    const struct layer_rgb_color color = layer_colors[layer];

    sync_peripherals_rgb(color);

    return zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){
        .h = color.h,
        .s = color.s,
        .b = color.b,
    });
}

ZMK_LISTENER(corne_layer_rgb, layer_rgb_listener);
ZMK_SUBSCRIPTION(corne_layer_rgb, zmk_layer_state_changed);
