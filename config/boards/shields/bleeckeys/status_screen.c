/*
 * Status screen for the left (central) OLED, 128x32:
 *   top:    BT symbol + profile          left battery, right-half icon, right battery
 *   bottom: layer name                   WPM graph
 * plus a volume bar on the bottom edge and the whole panel inverted while Caps Lock is on.
 * The boot animation and the matrix rain are drawn over it by display_fx.c.
 */

#include <zephyr/bluetooth/conn.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/display.h>
#include <zmk/display/status_screen.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/hid_indicators_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/wpm_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/wpm.h>

#include "display_fx.h"

#define ROW_H 16
#define ICON_W 7
#define ICON_H 10
#define ICON_Y 3
#define GAP 2
#define WIPE_W 11
#define WPM_W 40
#define WPM_H 12
#define WPM_BAR 4
#define WPM_MAX 100
#define VOLUME_GROW_MS 150
#define VOLUME_SHOW_MS 600

static lv_color_t fg;

static lv_obj_t *rect_create(lv_obj_t *parent) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_color(obj, fg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    return obj;
}

// Blinking: visible for the first and last quarter of each period.
static void blink_exec(void *obj, int32_t v) {
    if (v >= 128) {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void blink_start(lv_obj_t *obj, uint32_t period_ms, uint16_t count, lv_anim_ready_cb_t done) {
    if (lv_anim_get(obj, blink_exec) != NULL) {
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, blink_exec);
    lv_anim_set_values(&a, 255, 0);
    lv_anim_set_time(&a, period_ms / 2);
    lv_anim_set_playback_time(&a, period_ms / 2);
    lv_anim_set_repeat_count(&a, count);
    lv_anim_set_ready_cb(&a, done);
    lv_anim_start(&a);
}

static void blink_stop(lv_obj_t *obj) {
    lv_anim_del(obj, blink_exec);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

/* Output: BT symbol and profile number, wiped over on a profile switch, the symbol
 * pulsing while the active profile is not connected (searching or open for pairing). */

struct output_state {
    struct zmk_endpoint_instance endpoint;
    bool connected;
};

static lv_obj_t *conn_label;
static lv_obj_t *profile_label;
static lv_obj_t *wipe_rect;
static int shown_profile = -1;
static char next_profile[12];

// 0..WIPE_W: the bar covers the old number, WIPE_W..2*WIPE_W: it uncovers the new one.
static void wipe_exec(void *obj, int32_t v) {
    lv_coord_t x = lv_obj_get_x(profile_label);

    if (v >= WIPE_W) {
        if (strcmp(lv_label_get_text(profile_label), next_profile) != 0) {
            lv_label_set_text(profile_label, next_profile);
        }
        x += v - WIPE_W;
        v = 2 * WIPE_W - v;
    }
    lv_obj_set_x(obj, x);
    lv_obj_set_width(obj, v);
}

static void output_update_cb(struct output_state state) {
    bool ble = state.endpoint.transport == ZMK_TRANSPORT_BLE;
    int profile = state.endpoint.ble.profile_index;

    lv_label_set_text(conn_label, ble ? LV_SYMBOL_BLUETOOTH : LV_SYMBOL_USB);
    if (!ble) {
        lv_obj_add_flag(profile_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(profile_label, LV_OBJ_FLAG_HIDDEN);
        snprintf(next_profile, sizeof(next_profile), "%d", profile + 1);
        if (shown_profile >= 0 && profile != shown_profile) {
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, wipe_rect);
            lv_anim_set_exec_cb(&a, wipe_exec);
            lv_anim_set_values(&a, 0, 2 * WIPE_W);
            lv_anim_set_time(&a, 240);
            lv_anim_start(&a);
        } else if (lv_anim_get(wipe_rect, wipe_exec) == NULL) {
            lv_label_set_text(profile_label, next_profile);
        }
        shown_profile = profile;
    }

    if (ble && !state.connected) {
        blink_start(conn_label, 800, LV_ANIM_REPEAT_INFINITE, NULL);
    } else {
        blink_stop(conn_label);
    }
}

static struct output_state output_get_state(const zmk_event_t *eh) {
    return (struct output_state){
        .endpoint = zmk_endpoints_selected(),
        .connected = zmk_ble_active_profile_is_connected(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_output, struct output_state, output_update_cb, output_get_state)
ZMK_SUBSCRIPTION(widget_output, zmk_endpoint_changed);
ZMK_SUBSCRIPTION(widget_output, zmk_ble_active_profile_changed);

/* Batteries "L% R%" with the right-half icon between them. The icon slides in when the
 * right half connects and flashes for a few seconds when the link drops. A level below
 * 10% blinks. */

struct batteries_state {
    uint8_t left;
    int16_t right; // -1 while the right half is not connected or has not reported
    bool linked;
};

static lv_obj_t *left_label;
static lv_obj_t *right_label;
static lv_obj_t *link_icon;
static int16_t right_level = -1;
static bool right_linked;
static bool shown_linked;

static void set_level(lv_obj_t *label, int16_t level) {
    if (level < 0) {
        lv_label_set_text(label, "--");
    } else {
        lv_label_set_text_fmt(label, "%d%%", level);
    }

    if (level >= 0 && level < 10) {
        blink_start(label, 800, LV_ANIM_REPEAT_INFINITE, NULL);
    } else {
        blink_stop(label);
    }
}

static void icon_y_exec(void *obj, int32_t v) { lv_obj_set_y(obj, v); }

static void hide_when_done(lv_anim_t *a) { lv_obj_add_flag(a->var, LV_OBJ_FLAG_HIDDEN); }

static void batteries_update_cb(struct batteries_state state) {
    set_level(left_label, state.left);
    set_level(right_label, state.right);

    lv_obj_update_layout(right_label);
    lv_coord_t x = lv_obj_get_x(right_label) - GAP - ICON_W;
    lv_obj_set_x(link_icon, x);
    lv_obj_set_x(left_label, x - GAP - lv_obj_get_width(left_label));

    if (state.linked == shown_linked) {
        return;
    }
    shown_linked = state.linked;
    lv_anim_del(link_icon, blink_exec);
    lv_obj_clear_flag(link_icon, LV_OBJ_FLAG_HIDDEN);
    if (state.linked) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, link_icon);
        lv_anim_set_exec_cb(&a, icon_y_exec);
        lv_anim_set_values(&a, -ICON_H, ICON_Y);
        lv_anim_set_time(&a, 300);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    } else {
        lv_anim_del(link_icon, icon_y_exec);
        lv_obj_set_y(link_icon, ICON_Y);
        blink_start(link_icon, 400, 8, hide_when_done);
    }
}

static struct batteries_state batteries_get_state(const zmk_event_t *eh) {
    const struct zmk_peripheral_battery_state_changed *ev =
        eh != NULL ? as_zmk_peripheral_battery_state_changed(eh) : NULL;

    // ZMK reports 0% when the link drops; that is shown as "--" instead.
    if (ev != NULL && right_linked) {
        right_level = ev->state_of_charge;
    }

    return (struct batteries_state){
        .left = zmk_battery_state_of_charge(),
        .right = right_level,
        .linked = right_linked,
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_batteries, struct batteries_state, batteries_update_cb,
                            batteries_get_state)
ZMK_SUBSCRIPTION(widget_batteries, zmk_battery_state_changed);
ZMK_SUBSCRIPTION(widget_batteries, zmk_peripheral_battery_state_changed);

// The left half is the BLE central of the split link only; hosts connect to it as peripheral.
static void split_link_changed(struct bt_conn *conn, bool linked) {
    struct bt_conn_info info;

    if (bt_conn_get_info(conn, &info) == 0 && info.role == BT_CONN_ROLE_CENTRAL) {
        right_linked = linked;
        right_level = -1;
        widget_batteries_cb(NULL);
    }
}

static void split_connected(struct bt_conn *conn, uint8_t err) {
    if (err == 0) {
        split_link_changed(conn, true);
    }
}

static void split_disconnected(struct bt_conn *conn, uint8_t reason) {
    split_link_changed(conn, false);
}

BT_CONN_CB_DEFINE(split_link_callbacks) = {
    .connected = split_connected,
    .disconnected = split_disconnected,
};

/* Layer name: the new name slides in from below when the new layer is higher than the
 * previous one, from above when lower. */

struct layer_state {
    zmk_keymap_layer_index_t index;
    const char *name;
};

static lv_obj_t *layer_labels[2];
static uint8_t layer_front;
static int layer_shown = -1;
static int layer_dir;

// v runs from layer_dir * ROW_H to 0: the front label moves to 0, the back one out of the row.
static void layer_slide_exec(void *var, int32_t v) {
    lv_obj_set_y(layer_labels[layer_front], v);
    lv_obj_set_y(layer_labels[!layer_front], v - layer_dir * ROW_H);
}

static void layer_update_cb(struct layer_state state) {
    if (state.index == layer_shown) {
        return;
    }

    if (layer_shown >= 0) {
        lv_anim_del(NULL, layer_slide_exec);
        layer_front = !layer_front;
        layer_dir = state.index > layer_shown ? 1 : -1;

        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_exec_cb(&a, layer_slide_exec);
        lv_anim_set_values(&a, layer_dir * ROW_H, 0);
        lv_anim_set_time(&a, 200);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    }
    lv_label_set_text(layer_labels[layer_front], state.name != NULL ? state.name : "");
    layer_shown = state.index;
}

static struct layer_state layer_get_state(const zmk_event_t *eh) {
    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();

    return (struct layer_state){
        .index = index,
        .name = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index)),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_layer, struct layer_state, layer_update_cb, layer_get_state)
ZMK_SUBSCRIPTION(widget_layer, zmk_layer_state_changed);

/* Volume bar: a 2 px line on the bottom edge growing from the centre to the right on
 * volume up, to the left on volume down, hidden again after VOLUME_SHOW_MS. */

struct volume_state {
    uint8_t steps; // counts volume key presses, so each one animates once
    bool up;
};

static lv_obj_t *volume_bar;
static uint8_t volume_shown_steps;
static bool volume_up;

static void volume_exec(void *obj, int32_t v) {
    lv_coord_t w = MIN(v, VOLUME_GROW_MS) * (LV_HOR_RES / 2) / VOLUME_GROW_MS;

    lv_obj_set_x(obj, volume_up ? LV_HOR_RES / 2 : LV_HOR_RES / 2 - w);
    lv_obj_set_width(obj, w);
}

static void volume_update_cb(struct volume_state state) {
    if (state.steps == volume_shown_steps) {
        return;
    }
    volume_shown_steps = state.steps;
    volume_up = state.up;

    lv_obj_clear_flag(volume_bar, LV_OBJ_FLAG_HIDDEN);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, volume_bar);
    lv_anim_set_exec_cb(&a, volume_exec);
    lv_anim_set_values(&a, 0, VOLUME_SHOW_MS);
    lv_anim_set_time(&a, VOLUME_SHOW_MS);
    lv_anim_set_ready_cb(&a, hide_when_done);
    lv_anim_start(&a);
}

static struct volume_state volume_get_state(const zmk_event_t *eh) {
    static struct volume_state state;
    const struct zmk_keycode_state_changed *ev =
        eh != NULL ? as_zmk_keycode_state_changed(eh) : NULL;

    if (ev != NULL && ev->state && ev->usage_page == HID_USAGE_CONSUMER &&
        (ev->keycode == HID_USAGE_CONSUMER_VOLUME_INCREMENT ||
         ev->keycode == HID_USAGE_CONSUMER_VOLUME_DECREMENT)) {
        state.steps++;
        state.up = ev->keycode == HID_USAGE_CONSUMER_VOLUME_INCREMENT;
    }
    return state;
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_volume, struct volume_state, volume_update_cb, volume_get_state)
ZMK_SUBSCRIPTION(widget_volume, zmk_keycode_state_changed);

/* WPM graph: one bar per WPM update, newest on the right. */

static uint8_t wpm_buf[LV_CANVAS_BUF_SIZE_INDEXED_1BIT(WPM_W, WPM_H)] __aligned(4);
static uint8_t wpm_history[WPM_W / WPM_BAR];
static lv_obj_t *wpm_canvas;

static void wpm_update_cb(uint8_t wpm) {
    lv_img_dsc_t *img = lv_canvas_get_img(wpm_canvas);
    lv_color_t on = {.full = 1};

    memmove(wpm_history, wpm_history + 1, sizeof(wpm_history) - 1);
    wpm_history[sizeof(wpm_history) - 1] = MIN(wpm, WPM_MAX) * WPM_H / WPM_MAX;

    memset(wpm_buf + 2 * sizeof(lv_color32_t), 0, sizeof(wpm_buf) - 2 * sizeof(lv_color32_t));
    for (int i = 0; i < sizeof(wpm_history); i++) {
        for (int y = WPM_H - wpm_history[i]; y < WPM_H; y++) {
            for (int x = i * WPM_BAR; x < (i + 1) * WPM_BAR - 1; x++) {
                lv_img_buf_set_px_color(img, x, y, on);
            }
        }
    }
    lv_obj_invalidate(wpm_canvas);
}

static uint8_t wpm_get_state(const zmk_event_t *eh) { return zmk_wpm_get_state(); }

ZMK_DISPLAY_WIDGET_LISTENER(widget_wpm, uint8_t, wpm_update_cb, wpm_get_state)
ZMK_SUBSCRIPTION(widget_wpm, zmk_wpm_state_changed);

/* Caps Lock: the panel's own reverse-display command inverts everything on it. */

#define OLED DT_CHOSEN(zephyr_display)
#define SSD1306_CMD 0x00
#define SSD1306_NORMAL_DISPLAY 0xa6
#define SSD1306_REVERSE_DISPLAY 0xa7

static const struct i2c_dt_spec oled_bus = I2C_DT_SPEC_GET(OLED);

static void caps_update_cb(bool caps) {
    bool reverse = caps != DT_PROP(OLED, inversion_on);

    i2c_reg_write_byte_dt(&oled_bus, SSD1306_CMD,
                          reverse ? SSD1306_REVERSE_DISPLAY : SSD1306_NORMAL_DISPLAY);
}

static bool caps_get_state(const zmk_event_t *eh) {
    const struct zmk_hid_indicators_changed *ev =
        eh != NULL ? as_zmk_hid_indicators_changed(eh) : NULL;

    return ev != NULL && (ev->indicators & BIT(HID_USAGE_LED_CAPS_LOCK - 1));
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_caps, bool, caps_update_cb, caps_get_state)
ZMK_SUBSCRIPTION(widget_caps, zmk_hid_indicators_changed);

lv_obj_t *zmk_display_status_screen() {
    lv_obj_t *screen = lv_obj_create(NULL);
    fg = lv_obj_get_style_text_color(screen, LV_PART_MAIN);

    conn_label = lv_label_create(screen);
    lv_label_set_text(conn_label, LV_SYMBOL_BLUETOOTH);
    profile_label = lv_label_create(screen);
    lv_obj_align_to(profile_label, conn_label, LV_ALIGN_OUT_RIGHT_TOP, GAP, 0);
    wipe_rect = rect_create(screen);
    lv_obj_set_pos(wipe_rect, 0, 1);
    lv_obj_set_size(wipe_rect, 0, ROW_H - 2);
    widget_output_init();

    right_label = lv_label_create(screen);
    lv_obj_align(right_label, LV_ALIGN_TOP_RIGHT, 0, 0);
    left_label = lv_label_create(screen);
    link_icon = lv_obj_create(screen);
    lv_obj_remove_style_all(link_icon);
    lv_obj_set_size(link_icon, ICON_W, ICON_H);
    lv_obj_set_y(link_icon, ICON_Y);
    lv_obj_set_style_border_color(link_icon, fg, LV_PART_MAIN);
    lv_obj_set_style_border_width(link_icon, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(link_icon, 1, LV_PART_MAIN);
    lv_obj_add_flag(link_icon, LV_OBJ_FLAG_HIDDEN);
    widget_batteries_init();

    lv_obj_t *layer_row = lv_obj_create(screen);
    lv_obj_remove_style_all(layer_row);
    lv_obj_set_pos(layer_row, 0, ROW_H);
    lv_obj_set_size(layer_row, LV_HOR_RES - WPM_W - GAP, ROW_H);
    for (int i = 0; i < 2; i++) {
        layer_labels[i] = lv_label_create(layer_row);
        lv_obj_set_style_text_font(layer_labels[i], lv_theme_get_font_small(screen), LV_PART_MAIN);
        lv_label_set_text(layer_labels[i], "");
    }
    widget_layer_init();

    wpm_canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(wpm_canvas, wpm_buf, WPM_W, WPM_H, LV_IMG_CF_INDEXED_1BIT);
    lv_canvas_set_palette(wpm_canvas, 0, lv_obj_get_style_bg_color(screen, LV_PART_MAIN));
    lv_canvas_set_palette(wpm_canvas, 1, fg);
    lv_obj_align(wpm_canvas, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    widget_wpm_init();

    volume_bar = rect_create(screen);
    lv_obj_set_pos(volume_bar, LV_HOR_RES / 2, LV_VER_RES - 2);
    lv_obj_set_size(volume_bar, 0, 2);
    lv_obj_add_flag(volume_bar, LV_OBJ_FLAG_HIDDEN);
    widget_volume_init();

    widget_caps_init();

    display_fx_init(screen);
    return screen;
}
