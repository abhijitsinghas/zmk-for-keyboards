/*
 * Custom Corne OLED status screen, 128x32.
 *
 * Left/central half:
 *   top-left: USB icon when USB is active, otherwise Bluetooth icon + profile name + state mark
 *   top-right: battery outline with percent and lightning when USB-powered
 *   bottom/center: layer icon
 *
 * Right/peripheral half:
 *   top-left: Bluetooth icon + link-to-central state mark
 *   top-right: local battery outline with percent and lightning when USB-powered
 *   bottom/center: layer icon
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <lvgl.h>

#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/display.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/events/layer_state_changed.h>
#endif
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/keymap.h>
#endif
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/usb.h>

#define BATTERY_W 36
#define BATTERY_H 13
#define BATTERY_CAP_W 3
#define BATTERY_CAP_H 7

#define BT_ICON_W 9
#define BT_ICON_H 13
#define CURSOR_ICON_W 11
#define CURSOR_ICON_H 13

static lv_obj_t *conn_icon;
static lv_obj_t *conn_label;
static lv_obj_t *layer_label;
static lv_obj_t *layer_image;
static lv_obj_t *battery_body;
static lv_obj_t *battery_cap;
static lv_obj_t *battery_label;

static const char *profile_names[] = {"Mac", "Windows", "Tab", "Phone", "Spare"};

/* 1-bit bitmap rows, MSB-left. */
static const uint16_t bluetooth_rows[BT_ICON_H] = {
    0b000100000, 0b000110000, 0b000101000, 0b100100100, 0b010101000,
    0b001110000, 0b000100000, 0b001110000, 0b010101000, 0b100100100,
    0b000101000, 0b000110000, 0b000100000,
};

static const uint16_t cursor_rows[CURSOR_ICON_H] = {
    0b10000000000, 0b11000000000, 0b10100000000, 0b10010000000, 0b10001000000,
    0b10000100000, 0b10000010000, 0b10011111000, 0b11001000000, 0b01001000000,
    0b00100100000, 0b00100100000, 0b00011000000,
};

static lv_obj_t *create_pixel_icon(lv_obj_t *parent, const uint16_t *rows, uint8_t width,
                                   uint8_t height) {
    lv_obj_t *icon = lv_obj_create(parent);
    lv_obj_set_size(icon, width, height);
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(icon, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(icon, 0, LV_PART_MAIN);

    for (uint8_t y = 0; y < height; y++) {
        for (uint8_t x = 0; x < width; x++) {
            if (rows[y] & BIT(width - 1 - x)) {
                lv_obj_t *pixel = lv_obj_create(icon);
                lv_obj_set_size(pixel, 1, 1);
                lv_obj_align(pixel, LV_ALIGN_TOP_LEFT, x, y);
                lv_obj_set_style_bg_color(pixel, lv_color_white(), LV_PART_MAIN);
                lv_obj_set_style_bg_opa(pixel, LV_OPA_COVER, LV_PART_MAIN);
                lv_obj_set_style_border_width(pixel, 0, LV_PART_MAIN);
                lv_obj_set_style_radius(pixel, 0, LV_PART_MAIN);
                lv_obj_set_style_pad_all(pixel, 0, LV_PART_MAIN);
            }
        }
    }

    return icon;
}

static const char *profile_name(uint8_t index) {
    if (index < ARRAY_SIZE(profile_names)) {
        return profile_names[index];
    }
    return "Profile";
}

static const char *layer_label_icon(uint8_t index, const char *fallback) {
    switch (index) {
    case 0:
        return LV_SYMBOL_HOME; /* base/home */
    case 1:
        return "123"; /* number layer icon */
    case 2:
        return "{}"; /* symbol layer icon */
    case 4:
        return LV_SYMBOL_SETTINGS; /* config/adjust */
    default:
        return (fallback != NULL && strlen(fallback) > 0) ? fallback : "L?";
    }
}

static void style_label(lv_obj_t *label) {
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, LV_PART_MAIN);
}

static void init_conn_widget(lv_obj_t *screen) {
    conn_icon = create_pixel_icon(screen, bluetooth_rows, BT_ICON_W, BT_ICON_H);
    lv_obj_align(conn_icon, LV_ALIGN_TOP_LEFT, 0, 0);

    conn_label = lv_label_create(screen);
    style_label(conn_label);
    lv_obj_set_style_text_font(conn_label, lv_theme_get_font_small(screen), LV_PART_MAIN);
    lv_obj_align(conn_label, LV_ALIGN_TOP_LEFT, 12, 0);
}

static void init_battery_widget(lv_obj_t *screen) {
    battery_body = lv_obj_create(screen);
    lv_obj_set_size(battery_body, BATTERY_W, BATTERY_H);
    lv_obj_align(battery_body, LV_ALIGN_TOP_RIGHT, -BATTERY_CAP_W - 1, 0);
    lv_obj_set_style_bg_opa(battery_body, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(battery_body, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(battery_body, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(battery_body, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(battery_body, 0, LV_PART_MAIN);

    battery_cap = lv_obj_create(screen);
    lv_obj_set_size(battery_cap, BATTERY_CAP_W, BATTERY_CAP_H);
    lv_obj_align(battery_cap, LV_ALIGN_TOP_RIGHT, 0, 3);
    lv_obj_set_style_bg_color(battery_cap, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(battery_cap, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(battery_cap, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(battery_cap, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(battery_cap, 0, LV_PART_MAIN);

    battery_label = lv_label_create(battery_body);
    style_label(battery_label);
    lv_obj_set_style_text_font(battery_label, lv_theme_get_font_small(screen), LV_PART_MAIN);
    lv_obj_center(battery_label);
}

static void init_layer_widget(lv_obj_t *screen) {
    layer_label = lv_label_create(screen);
    style_label(layer_label);
    lv_obj_set_style_text_font(layer_label, lv_theme_get_font_small(screen), LV_PART_MAIN);
    lv_obj_align(layer_label, LV_ALIGN_BOTTOM_MID, 0, 0);

    layer_image = create_pixel_icon(screen, cursor_rows, CURSOR_ICON_W, CURSOR_ICON_H);
    lv_obj_align(layer_image, LV_ALIGN_BOTTOM_MID, 0, -1);
    lv_obj_add_flag(layer_image, LV_OBJ_FLAG_HIDDEN);
}

struct battery_ui_state {
    uint8_t level;
    bool charging;
};

static struct battery_ui_state battery_get_state(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);

    return (struct battery_ui_state){
        .level = (ev != NULL) ? ev->state_of_charge : zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .charging = zmk_usb_is_powered(),
#else
        .charging = false,
#endif
    };
}

static void battery_update_cb(struct battery_ui_state state) {
    char text[12];

    if (state.charging) {
        snprintf(text, sizeof(text), LV_SYMBOL_CHARGE "%u%%", state.level);
    } else {
        snprintf(text, sizeof(text), "%u%%", state.level);
    }

    lv_label_set_text(battery_label, text);
    lv_obj_center(battery_label);
}

ZMK_DISPLAY_WIDGET_LISTENER(corne_battery_ui, struct battery_ui_state, battery_update_cb,
                            battery_get_state)
ZMK_SUBSCRIPTION(corne_battery_ui, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(corne_battery_ui, zmk_usb_conn_state_changed);
#endif

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

struct layer_ui_state {
    uint8_t index;
    const char *label;
};

static struct layer_ui_state layer_get_state(const zmk_event_t *eh) {
    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();
    return (struct layer_ui_state){
        .index = index,
        .label = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index)),
    };
}

static void layer_update_cb(struct layer_ui_state state) {
    if (state.index == 3) {
        lv_obj_add_flag(layer_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(layer_image, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(layer_image, LV_ALIGN_BOTTOM_MID, 0, -1);
        return;
    }

    lv_obj_add_flag(layer_image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(layer_label, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(layer_label, layer_label_icon(state.index, state.label));
    lv_obj_align(layer_label, LV_ALIGN_BOTTOM_MID, 0, 0);
}

ZMK_DISPLAY_WIDGET_LISTENER(corne_layer_ui, struct layer_ui_state, layer_update_cb, layer_get_state)
ZMK_SUBSCRIPTION(corne_layer_ui, zmk_layer_state_changed);

struct conn_ui_state {
    struct zmk_endpoint_instance selected_endpoint;
    int active_profile_index;
    bool active_profile_connected;
    bool active_profile_bonded;
};

static struct conn_ui_state conn_get_state(const zmk_event_t *eh) {
    return (struct conn_ui_state){
        .selected_endpoint = zmk_endpoint_get_selected(),
        .active_profile_index = zmk_ble_active_profile_index(),
        .active_profile_connected = zmk_ble_active_profile_is_connected(),
        .active_profile_bonded = !zmk_ble_active_profile_is_open(),
    };
}

static void conn_update_cb(struct conn_ui_state state) {
    char text[28] = {};

    if (state.selected_endpoint.transport == ZMK_TRANSPORT_USB) {
        lv_obj_add_flag(conn_icon, LV_OBJ_FLAG_HIDDEN);
        snprintf(text, sizeof(text), LV_SYMBOL_USB);
        lv_label_set_text(conn_label, text);
        lv_obj_align(conn_label, LV_ALIGN_TOP_LEFT, 0, 0);
    } else {
        uint8_t profile = state.active_profile_index >= 0 ? state.active_profile_index : 0;
        const char *mark = state.active_profile_connected ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE;

        if (!state.active_profile_bonded) {
            mark = LV_SYMBOL_SETTINGS;
        }

        lv_obj_clear_flag(conn_icon, LV_OBJ_FLAG_HIDDEN);
        snprintf(text, sizeof(text), "%s %s", profile_name(profile), mark);
        lv_label_set_text(conn_label, text);
        lv_obj_align(conn_label, LV_ALIGN_TOP_LEFT, 12, 0);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(corne_conn_ui, struct conn_ui_state, conn_update_cb, conn_get_state)
ZMK_SUBSCRIPTION(corne_conn_ui, zmk_endpoint_changed);
#if IS_ENABLED(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(corne_conn_ui, zmk_ble_active_profile_changed);
#endif

#else /* peripheral/right side */

struct conn_ui_state {
    bool connected;
};

static struct conn_ui_state conn_get_state(const zmk_event_t *eh) {
    return (struct conn_ui_state){.connected = zmk_split_bt_peripheral_is_connected()};
}

static void conn_update_cb(struct conn_ui_state state) {
    lv_obj_clear_flag(conn_icon, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(conn_label, state.connected ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
    lv_obj_align(conn_label, LV_ALIGN_TOP_LEFT, 12, 0);
}

ZMK_DISPLAY_WIDGET_LISTENER(corne_conn_ui, struct conn_ui_state, conn_update_cb, conn_get_state)
ZMK_SUBSCRIPTION(corne_conn_ui, zmk_split_peripheral_status_changed);

#endif

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    init_conn_widget(screen);
    init_battery_widget(screen);
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    init_layer_widget(screen);
#endif

    corne_conn_ui_init();
    corne_battery_ui_init();
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    corne_layer_ui_init();
#endif

    return screen;
}
