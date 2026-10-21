/*
 * Battery level with this keyboard's own range: reads the voltage from the
 * board's battery sensor and maps min-mv..max-mv linearly to 0..100%.
 */

#define DT_DRV_COMPAT bleeckeys_battery

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct battery_config {
    const struct device *source;
    int min_mv;
    int max_mv;
};

struct battery_data {
    int millivolts;
    uint8_t state_of_charge;
};

static int battery_sample_fetch(const struct device *dev, enum sensor_channel chan) {
    const struct battery_config *cfg = dev->config;
    struct battery_data *data = dev->data;
    struct sensor_value voltage;

    if (chan != SENSOR_CHAN_GAUGE_VOLTAGE && chan != SENSOR_CHAN_GAUGE_STATE_OF_CHARGE &&
        chan != SENSOR_CHAN_ALL) {
        return -ENOTSUP;
    }

    int rc = sensor_sample_fetch_chan(cfg->source, SENSOR_CHAN_GAUGE_VOLTAGE);
    if (rc == 0) {
        rc = sensor_channel_get(cfg->source, SENSOR_CHAN_GAUGE_VOLTAGE, &voltage);
    }
    if (rc != 0) {
        return rc;
    }

    data->millivolts = voltage.val1 * 1000 + voltage.val2 / 1000;
    data->state_of_charge =
        CLAMP((data->millivolts - cfg->min_mv) * 100 / (cfg->max_mv - cfg->min_mv), 0, 100);

    LOG_DBG("%d mV => %d%%", data->millivolts, data->state_of_charge);

    return 0;
}

static int battery_channel_get(const struct device *dev, enum sensor_channel chan,
                               struct sensor_value *val) {
    const struct battery_data *data = dev->data;

    switch (chan) {
    case SENSOR_CHAN_GAUGE_VOLTAGE:
        val->val1 = data->millivolts / 1000;
        val->val2 = (data->millivolts % 1000) * 1000;
        return 0;
    case SENSOR_CHAN_GAUGE_STATE_OF_CHARGE:
        val->val1 = data->state_of_charge;
        val->val2 = 0;
        return 0;
    default:
        return -ENOTSUP;
    }
}

static const struct sensor_driver_api battery_api = {
    .sample_fetch = battery_sample_fetch,
    .channel_get = battery_channel_get,
};

static const struct battery_config battery_config = {
    .source = DEVICE_DT_GET(DT_INST_PHANDLE(0, voltage_source)),
    .min_mv = DT_INST_PROP(0, min_mv),
    .max_mv = DT_INST_PROP(0, max_mv),
};

static struct battery_data battery_data;

DEVICE_DT_INST_DEFINE(0, NULL, NULL, &battery_data, &battery_config, POST_KERNEL,
                      CONFIG_SENSOR_INIT_PRIORITY, &battery_api);
