/*
 * Status screen for the left (central) OLED: ZMK's built-in layout, with the
 * battery widget replaced by one that shows both halves, left then right.
 */

#include <stdio.h>
#include <zephyr/kernel.h>

#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/display/status_screen.h>
#include <zmk/display/widgets/layer_status.h>
#include <zmk/display/widgets/output_status.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>

struct batteries_state {
    uint8_t left;
    int16_t right; // -1 until the right half has reported
};

static lv_obj_t *batteries_label;
static int16_t right_level = -1;

static void batteries_update_cb(struct batteries_state state) {
    char text[12];

    if (state.right < 0) {
        snprintf(text, sizeof(text), "%u%% --", state.left);
    } else {
        snprintf(text, sizeof(text), "%u%% %u%%", state.left, state.right);
    }
    lv_label_set_text(batteries_label, text);
}

static struct batteries_state batteries_get_state(const zmk_event_t *eh) {
    const struct zmk_peripheral_battery_state_changed *ev =
        as_zmk_peripheral_battery_state_changed(eh);

    if (ev != NULL) {
        right_level = ev->state_of_charge;
    }

    return (struct batteries_state){
        .left = zmk_battery_state_of_charge(),
        .right = right_level,
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_batteries, struct batteries_state, batteries_update_cb,
                            batteries_get_state)

ZMK_SUBSCRIPTION(widget_batteries, zmk_battery_state_changed);
ZMK_SUBSCRIPTION(widget_batteries, zmk_peripheral_battery_state_changed);

static struct zmk_widget_output_status output_status_widget;
static struct zmk_widget_layer_status layer_status_widget;

lv_obj_t *zmk_display_status_screen() {
    lv_obj_t *screen = lv_obj_create(NULL);

    batteries_label = lv_label_create(screen);
    lv_obj_align(batteries_label, LV_ALIGN_TOP_RIGHT, 0, 0);
    widget_batteries_init();

    zmk_widget_output_status_init(&output_status_widget, screen);
    lv_obj_align(zmk_widget_output_status_obj(&output_status_widget), LV_ALIGN_TOP_LEFT, 0, 0);

    zmk_widget_layer_status_init(&layer_status_widget, screen);
    lv_obj_set_style_text_font(zmk_widget_layer_status_obj(&layer_status_widget),
                               lv_theme_get_font_small(screen), LV_PART_MAIN);
    lv_obj_align(zmk_widget_layer_status_obj(&layer_status_widget), LV_ALIGN_BOTTOM_LEFT, 0, 0);

    return screen;
}
