/*
 * Trackpad behaviour on the cursor path (after absolute_to_relative):
 *   tap                         -> click (sent once double-tap-ms passes)
 *   tap, tap                    -> double click
 *   tap, then touch and hold    -> button held while dragging, released on lift
 *   finger at the edge          -> cursor keeps moving that way (edge motion)
 */

#define DT_DRV_COMPAT bleeckeys_trackpad

#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/input_processor.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

enum tap_state {
    TAP_IDLE,
    TAP_FIRST_TOUCH, // finger down, might become a tap
    TAP_TAPPED,      // tapped, waiting double-tap-ms for a second touch
    TAP_SECOND_TOUCH // second touch after a tap: button is held
};

struct trackpad_config {
    int32_t width;
    int32_t height;
    int32_t tap_ms;
    int32_t tap_max_move;
    int32_t double_tap_ms;
    int32_t touch_end_ms;
    int32_t edge_margin;
    int32_t edge_speed;
};

struct trackpad_data {
    const struct device *dev;
    struct k_mutex lock;
    enum tap_state state;
    bool touching;
    int64_t touch_start;
    int64_t last_event;
    int32_t moved;
    int32_t x;
    int32_t y;
    bool been_inside; // edge motion only after the finger was inside the border
    struct k_work_delayable touch_end_work;
    struct k_work_delayable tap_window_work;
};

static void set_button(bool pressed) {
    if (pressed) {
        zmk_hid_mouse_button_press(0);
    } else {
        zmk_hid_mouse_button_release(0);
    }
    zmk_endpoints_send_mouse_report();
}

static void click(void) {
    set_button(true);
    set_button(false);
}

static void touch_start(const struct trackpad_config *cfg, struct trackpad_data *data,
                        int64_t now) {
    data->touching = true;
    data->touch_start = now;
    data->moved = 0;
    data->been_inside = false;

    if (data->state == TAP_TAPPED) {
        k_work_cancel_delayable(&data->tap_window_work);
        set_button(true);
        data->state = TAP_SECOND_TOUCH;
    } else {
        data->state = TAP_FIRST_TOUCH;
    }
}

static void touch_end_cb(struct k_work *work) {
    struct k_work_delayable *d_work = k_work_delayable_from_work(work);
    struct trackpad_data *data = CONTAINER_OF(d_work, struct trackpad_data, touch_end_work);
    const struct trackpad_config *cfg = data->dev->config;

    k_mutex_lock(&data->lock, K_FOREVER);
    data->touching = false;
    bool tap = data->last_event - data->touch_start <= cfg->tap_ms &&
               data->moved <= cfg->tap_max_move;

    switch (data->state) {
    case TAP_FIRST_TOUCH:
        if (tap) {
            data->state = TAP_TAPPED;
            k_work_reschedule(&data->tap_window_work, K_MSEC(cfg->double_tap_ms));
        } else {
            data->state = TAP_IDLE;
        }
        break;
    case TAP_SECOND_TOUCH:
        set_button(false);
        if (tap) {
            click(); // second click of the double click
        }
        data->state = TAP_IDLE;
        break;
    default:
        break;
    }
    k_mutex_unlock(&data->lock);
}

static void tap_window_cb(struct k_work *work) {
    struct k_work_delayable *d_work = k_work_delayable_from_work(work);
    struct trackpad_data *data = CONTAINER_OF(d_work, struct trackpad_data, tap_window_work);

    k_mutex_lock(&data->lock, K_FOREVER);
    if (data->state == TAP_TAPPED) {
        click();
        data->state = TAP_IDLE;
    }
    k_mutex_unlock(&data->lock);
}

static int32_t edge_push(int32_t pos, int32_t size, int32_t margin, int32_t speed) {
    if (pos <= margin) {
        return -speed;
    }
    if (pos >= size - margin) {
        return speed;
    }
    return 0;
}

static int trackpad_handle_event(const struct device *dev, struct input_event *event,
                                 uint32_t param1, uint32_t param2,
                                 struct zmk_input_processor_state *state) {
    const struct trackpad_config *cfg = dev->config;
    struct trackpad_data *data = dev->data;
    int64_t now = k_uptime_get();

    if (event->type != INPUT_EV_ABS && event->type != INPUT_EV_REL) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    k_mutex_lock(&data->lock, K_FOREVER);
    k_work_reschedule(&data->touch_end_work, K_MSEC(cfg->touch_end_ms));
    if (!data->touching) {
        touch_start(cfg, data, now);
    }
    data->last_event = now;

    // absolute_to_relative passes the first report of a touch through as ABS
    if (event->type == INPUT_EV_ABS) {
        if (event->code == INPUT_ABS_X) {
            data->x = event->value;
        } else if (event->code == INPUT_ABS_Y) {
            data->y = event->value;
        }
        k_mutex_unlock(&data->lock);
        return ZMK_INPUT_PROC_CONTINUE;
    }

    if (event->code == INPUT_REL_X) {
        data->x = CLAMP(data->x + event->value, 0, cfg->width);
    } else if (event->code == INPUT_REL_Y) {
        data->y = CLAMP(data->y + event->value, 0, cfg->height);
    }
    data->moved += abs(event->value);

    bool inside = data->x > cfg->edge_margin && data->x < cfg->width - cfg->edge_margin &&
                  data->y > cfg->edge_margin && data->y < cfg->height - cfg->edge_margin;
    if (inside) {
        data->been_inside = true;
    } else if (data->been_inside) {
        if (event->code == INPUT_REL_X) {
            event->value += edge_push(data->x, cfg->width, cfg->edge_margin, cfg->edge_speed);
        } else if (event->code == INPUT_REL_Y) {
            event->value += edge_push(data->y, cfg->height, cfg->edge_margin, cfg->edge_speed);
        }
    }

    k_mutex_unlock(&data->lock);
    return ZMK_INPUT_PROC_CONTINUE;
}

static struct zmk_input_processor_driver_api trackpad_driver_api = {
    .handle_event = trackpad_handle_event,
};

static int trackpad_init(const struct device *dev) {
    struct trackpad_data *data = dev->data;

    data->dev = dev;
    k_mutex_init(&data->lock);
    k_work_init_delayable(&data->touch_end_work, touch_end_cb);
    k_work_init_delayable(&data->tap_window_work, tap_window_cb);
    return 0;
}

#define TRACKPAD_INST(n)                                                                           \
    static const struct trackpad_config trackpad_config_##n = {                                    \
        .width = DT_INST_PROP(n, width),                                                           \
        .height = DT_INST_PROP(n, height),                                                         \
        .tap_ms = DT_INST_PROP(n, tap_ms),                                                         \
        .tap_max_move = DT_INST_PROP(n, tap_max_move),                                             \
        .double_tap_ms = DT_INST_PROP(n, double_tap_ms),                                           \
        .touch_end_ms = DT_INST_PROP(n, touch_end_ms),                                             \
        .edge_margin = DT_INST_PROP(n, edge_margin),                                               \
        .edge_speed = DT_INST_PROP(n, edge_speed),                                                 \
    };                                                                                             \
    static struct trackpad_data trackpad_data_##n;                                                 \
    DEVICE_DT_INST_DEFINE(n, &trackpad_init, NULL, &trackpad_data_##n, &trackpad_config_##n,       \
                          POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &trackpad_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TRACKPAD_INST)
