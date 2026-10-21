/*
 * One swipe = one tap: counts trackpad movement per touch and, when one axis
 * reaches tick, taps the binding for that direction once. Movement never carries
 * over to the next touch, and a long swipe still fires only once.
 */

#define DT_DRV_COMPAT bleeckeys_swipe

#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/input_processor.h>
#include <zmk/behavior.h>
#include <zmk/keymap.h>
#include <zmk/virtual_key_position.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// A gap this long between events means the finger was lifted (or the layer just
// turned on mid-touch), so the next event starts a new swipe.
#define NEW_TOUCH_GAP_MS 100
#define TAP_MS 30

enum swipe_direction { SWIPE_RIGHT, SWIPE_LEFT, SWIPE_UP, SWIPE_DOWN };

struct swipe_config {
    uint8_t index;
    int32_t tick;
    const struct zmk_behavior_binding *bindings;
};

struct swipe_data {
    int32_t dx;
    int32_t dy;
    bool fired;
    int64_t last_event;
    struct zmk_behavior_binding_event pressed_event;
    const struct zmk_behavior_binding *pressed_binding;
    struct k_work_delayable release_work;
};

static void swipe_release(struct k_work *work) {
    struct k_work_delayable *d_work = k_work_delayable_from_work(work);
    struct swipe_data *data = CONTAINER_OF(d_work, struct swipe_data, release_work);

    data->pressed_event.timestamp = k_uptime_get();
    zmk_behavior_invoke_binding(data->pressed_binding, data->pressed_event, false);
}

static void swipe_tap(const struct swipe_config *cfg, struct swipe_data *data,
                      enum swipe_direction direction, uint8_t listener_index) {
    data->pressed_binding = &cfg->bindings[direction];
    data->pressed_event = (struct zmk_behavior_binding_event){
        .position = ZMK_VIRTUAL_KEY_POSITION_BEHAVIOR_INPUT_PROCESSOR(listener_index, cfg->index),
        .timestamp = k_uptime_get(),
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
#endif
    };

    LOG_DBG("swipe %d (dx %d dy %d)", direction, data->dx, data->dy);
    zmk_behavior_invoke_binding(data->pressed_binding, data->pressed_event, true);
    k_work_reschedule(&data->release_work, K_MSEC(TAP_MS));
}

static int swipe_handle_event(const struct device *dev, struct input_event *event,
                              uint32_t param1, uint32_t param2,
                              struct zmk_input_processor_state *state) {
    const struct swipe_config *cfg = dev->config;
    struct swipe_data *data = dev->data;
    int64_t now = k_uptime_get();

    // absolute_to_relative passes the first report of a touch through as ABS
    if (event->type == INPUT_EV_ABS || now - data->last_event > NEW_TOUCH_GAP_MS) {
        data->dx = 0;
        data->dy = 0;
        data->fired = false;
    }
    data->last_event = now;

    if (event->type != INPUT_EV_REL || data->fired) {
        return ZMK_INPUT_PROC_STOP;
    }

    if (event->code == INPUT_REL_X) {
        data->dx += event->value;
    } else if (event->code == INPUT_REL_Y) {
        data->dy += event->value;
    }

    if (abs(data->dx) < cfg->tick && abs(data->dy) < cfg->tick) {
        return ZMK_INPUT_PROC_STOP;
    }

    data->fired = true;
    if (abs(data->dx) >= abs(data->dy)) {
        swipe_tap(cfg, data, data->dx > 0 ? SWIPE_RIGHT : SWIPE_LEFT, state->input_device_index);
    } else {
        swipe_tap(cfg, data, data->dy > 0 ? SWIPE_DOWN : SWIPE_UP, state->input_device_index);
    }

    return ZMK_INPUT_PROC_STOP;
}

static struct zmk_input_processor_driver_api swipe_driver_api = {
    .handle_event = swipe_handle_event,
};

static int swipe_init(const struct device *dev) {
    struct swipe_data *data = dev->data;

    k_work_init_delayable(&data->release_work, swipe_release);
    return 0;
}

#define SWIPE_INST(n)                                                                              \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, bindings) == 4,                                               \
                 "bindings must be right, left, up, down");                                        \
    static const struct zmk_behavior_binding swipe_bindings_##n[] = {                              \
        LISTIFY(DT_INST_PROP_LEN(n, bindings), ZMK_KEYMAP_EXTRACT_BINDING, (, ), DT_DRV_INST(n))}; \
    static const struct swipe_config swipe_config_##n = {                                          \
        .index = n,                                                                                \
        .tick = DT_INST_PROP(n, tick),                                                             \
        .bindings = swipe_bindings_##n,                                                            \
    };                                                                                             \
    static struct swipe_data swipe_data_##n;                                                       \
    DEVICE_DT_INST_DEFINE(n, &swipe_init, NULL, &swipe_data_##n, &swipe_config_##n, POST_KERNEL,   \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &swipe_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SWIPE_INST)
