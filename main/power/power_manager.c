#include "power_manager.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define POWER_POLL_MS 500u
#define BATTERY_SAMPLE_MS 5000u

static power_event_cb_t s_cb;
static void *s_ctx;

static adc_oneshot_unit_handle_t s_adc;
static bool s_adc_ready;
static uint16_t s_battery_mv;

static bool input_active(int gpio, bool active_high)
{
    if (gpio < 0) {
        return false;
    }

    const int level = gpio_get_level((gpio_num_t)gpio);
    return active_high ? level != 0 : level == 0;
}

static int sample_adc(adc_channel_t channel)
{
    if (!s_adc_ready) {
        return -1;
    }

    int total = 0;
    const int sample_count = 8;

    for (int i = 0; i < sample_count; ++i) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, channel, &raw) != ESP_OK) {
            return -1;
        }
        total += raw;
    }

    return total / sample_count;
}

static bool charger_stat_high(void)
{
    const int raw =
        sample_adc((adc_channel_t)CONFIG_WEARABLE_CHARGER_STAT_ADC_CHANNEL);

    return raw >= CONFIG_WEARABLE_CHARGER_STAT_HIGH_RAW;
}

/*
 * The current PCB has no dedicated Vbat divider, so this function normally
 * returns 0. The compile-time hook is intentionally kept for a future board
 * revision; until that divider and its exact ratio are known, the firmware
 * must not pretend that MCP73831 STAT is battery voltage.
 */
static uint16_t sample_battery_mv(void)
{
#if CONFIG_WEARABLE_BATTERY_VOLTAGE_ADC_CHANNEL >= 0
    const int raw =
        sample_adc((adc_channel_t)CONFIG_WEARABLE_BATTERY_VOLTAGE_ADC_CHANNEL);
    if (raw < 0) {
        return 0;
    }

    /*
     * This path is disabled in the current hardware configuration. Do not
     * enable it until a calibrated Vbat divider is defined for the PCB.
     */
    const uint32_t pin_mv = (uint32_t)raw * 2500u / 4095u;
    return (uint16_t)(
        pin_mv *
        CONFIG_WEARABLE_BATTERY_DIVIDER_NUM /
        CONFIG_WEARABLE_BATTERY_DIVIDER_DEN);
#else
    return 0;
#endif
}

static void set_charge_led(bool on)
{
#if CONFIG_WEARABLE_CHARGE_LED_GPIO >= 0
    gpio_set_level((gpio_num_t)CONFIG_WEARABLE_CHARGE_LED_GPIO, on ? 1 : 0);
#else
    (void)on;
#endif
}

static void power_task(void *arg)
{
    (void)arg;

    uint32_t battery_sample_elapsed = BATTERY_SAMPLE_MS;
    uint32_t low_battery_elapsed = 0;
    bool blink = false;
    bool last_charging = false;
    bool last_full = false;

    for (;;) {
        const bool stat_high = charger_stat_high();

        bool vbus_present = false;
#if CONFIG_WEARABLE_VBUS_SENSE_GPIO >= 0
        vbus_present = input_active(
            CONFIG_WEARABLE_VBUS_SENSE_GPIO,
#if CONFIG_WEARABLE_VBUS_SENSE_ACTIVE_HIGH
            true
#else
            false
#endif
        );
#endif

        /*
         * MCP73831 STAT:
         *   LOW    -> charging
         *   HIGH   -> charge complete
         *   High-Z -> charger not powered
         *
         * R11 pulls battery_state to ground while STAT is High-Z. Therefore a
         * LOW ADC value alone cannot distinguish "charging" from "no VBUS".
         * Charge-complete, however, is unambiguous because STAT is HIGH.
         */
        const bool full = stat_high;
#if CONFIG_WEARABLE_VBUS_SENSE_GPIO >= 0
        const bool charging = vbus_present && !stat_high;
#else
        const bool charging = false;
#endif

        if (charging) {
            blink = !blink;
            set_charge_led(blink);
        } else if (full) {
            set_charge_led(true);
        } else {
            set_charge_led(false);
            blink = false;
        }

        if (charging && !last_charging && s_cb != NULL) {
            s_cb(POWER_EVENT_CHARGING, s_ctx);
        }
        if (full && !last_full && s_cb != NULL) {
            s_cb(POWER_EVENT_FULL, s_ctx);
        }

        last_charging = charging;
        last_full = full;

        battery_sample_elapsed += POWER_POLL_MS;
        low_battery_elapsed += POWER_POLL_MS;

        if (battery_sample_elapsed >= BATTERY_SAMPLE_MS) {
            battery_sample_elapsed = 0;
            s_battery_mv = sample_battery_mv();
        }

        if (s_battery_mv > 0 &&
            s_battery_mv <= CONFIG_WEARABLE_LOW_BATTERY_MV &&
            !vbus_present &&
            low_battery_elapsed >= CONFIG_WEARABLE_LOW_BATTERY_BEEP_MS) {
            low_battery_elapsed = 0;
            if (s_cb != NULL) {
                s_cb(POWER_EVENT_LOW_BATTERY, s_ctx);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(POWER_POLL_MS));
    }
}

esp_err_t power_manager_start(power_event_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_ctx = ctx;

#if CONFIG_WEARABLE_CHARGE_LED_GPIO >= 0
    gpio_config_t led = {
        .pin_bit_mask = 1ULL << CONFIG_WEARABLE_CHARGE_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&led);
    if (err != ESP_OK) {
        return err;
    }
#endif

#if CONFIG_WEARABLE_VBUS_SENSE_GPIO >= 0
    gpio_config_t vbus = {
        .pin_bit_mask = 1ULL << CONFIG_WEARABLE_VBUS_SENSE_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&vbus);
    if (err != ESP_OK) {
        return err;
    }
#endif

    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
    };

    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_adc);
    if (err != ESP_OK) {
        return err;
    }

    adc_oneshot_chan_cfg_t channel_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    err = adc_oneshot_config_channel(
        s_adc,
        (adc_channel_t)CONFIG_WEARABLE_CHARGER_STAT_ADC_CHANNEL,
        &channel_cfg);
    if (err != ESP_OK) {
        return err;
    }

#if CONFIG_WEARABLE_BATTERY_VOLTAGE_ADC_CHANNEL >= 0
    err = adc_oneshot_config_channel(
        s_adc,
        (adc_channel_t)CONFIG_WEARABLE_BATTERY_VOLTAGE_ADC_CHANNEL,
        &channel_cfg);
    if (err != ESP_OK) {
        return err;
    }
#endif

    s_adc_ready = true;

    if (xTaskCreate(power_task, "power", 3072, NULL, 3, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

uint16_t power_manager_battery_mv(void)
{
    return s_battery_mv;
}
