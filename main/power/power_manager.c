#include "power_manager.h"

#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static power_event_cb_t s_cb;
static void *s_ctx;

static adc_oneshot_unit_handle_t s_adc;
static bool s_adc_ready;
static uint16_t s_battery_mv;

static bool input_active(int gpio, bool active_high)
{
    if (gpio < 0) return false;
    int v = gpio_get_level((gpio_num_t)gpio);
    return active_high ? (v != 0) : (v == 0);
}

static int sample_adc(adc_channel_t channel)
{
    if (!s_adc_ready) return -1;

    /* Average a few samples. For STAT we only need to distinguish ~0 V from
       ~2.5 V, so the absolute ADC calibration error is irrelevant. */
    int total = 0;
    const int samples = 8;
    for (int i = 0; i < samples; ++i) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, channel, &raw) != ESP_OK) return -1;
        total += raw;
    }
    return total / samples;
}

static bool charger_stat_high(void)
{
    int raw = sample_adc((adc_channel_t)CONFIG_WEARABLE_CHARGER_STAT_ADC_CHANNEL);
    return raw >= CONFIG_WEARABLE_CHARGER_STAT_HIGH_RAW;
}

/*
 * A real battery-voltage measurement is deliberately disabled until the PCB
 * exposes Vbat through its own divider. "battery_state" is MCP73831 STAT, not
 * battery voltage, therefore it must never be used to estimate state of charge.
 */
static uint16_t sample_battery_mv(void)
{
#if CONFIG_WEARABLE_BATTERY_VOLTAGE_ADC_CHANNEL >= 0
    int raw = sample_adc((adc_channel_t)CONFIG_WEARABLE_BATTERY_VOLTAGE_ADC_CHANNEL);
    if (raw < 0) return 0;

    /* Placeholder linear conversion: this path is disabled on the current PCB.
       When the Vbat divider is added, replace this with ADC calibration plus
       the exact divider ratio before enabling the Kconfig channel. */
    uint32_t pin_mv = (uint32_t)raw * 2500u / 4095u;
    return (uint16_t)(pin_mv * CONFIG_WEARABLE_BATTERY_DIVIDER_NUM /
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

    uint32_t battery_sample_elapsed = 0;
    uint32_t low_battery_elapsed = 0;
    bool blink = false;
    bool last_charging = false;
    bool last_full = false;

    for (;;) {
        const bool stat_high = charger_stat_high();

        bool vbus_present = false;
#if CONFIG_WEARABLE_VBUS_SENSE_GPIO >= 0
        vbus_present = input_active(CONFIG_WEARABLE_VBUS_SENSE_GPIO,
#if CONFIG_WEARABLE_VBUS_SENSE_ACTIVE_HIGH
                                    true
#else
                                    false
#endif
                                    );
#endif

        /*
         * MCP73831:
         *   charging          -> STAT LOW
         *   charge complete   -> STAT HIGH
         *   VDD absent        -> STAT High-Z
         *
         * R11 pulls battery_state to GND when STAT is High-Z. Therefore LOW
         * alone is ambiguous: it means either "charging" or "charger absent".
         */
        const bool full = stat_high;
        const bool charging =
#if CONFIG_WEARABLE_VBUS_SENSE_GPIO >= 0
            vbus_present && !stat_high;
#else
            false;
#endif

        if (charging) {
            blink = !blink;
            set_charge_led(blink);
        } else if (full) {
            set_charge_led(true);
        } else {
            set_charge_led(false);
        }

        if (charging && !last_charging && s_cb) {
            s_cb(POWER_EVENT_CHARGING, s_ctx);
        }
        if (full && !last_full && s_cb) {
            s_cb(POWER_EVENT_FULL, s_ctx);
        }
        last_charging = charging;
        last_full = full;

        battery_sample_elapsed += 500;
        low_battery_elapsed += 500;

        if (battery_sample_elapsed >= 5000) {
            battery_sample_elapsed = 0;
            s_battery_mv = sample_battery_mv();
        }

        if (s_battery_mv > 0 &&
            s_battery_mv <= CONFIG_WEARABLE_LOW_BATTERY_MV &&
            !vbus_present &&
            low_battery_elapsed >= CONFIG_WEARABLE_LOW_BATTERY_BEEP_MS) {
            low_battery_elapsed = 0;
            if (s_cb) s_cb(POWER_EVENT_LOW_BATTERY, s_ctx);
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void power_manager_start(power_event_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_ctx = ctx;

#if CONFIG_WEARABLE_CHARGE_LED_GPIO >= 0
    gpio_config_t led = {
        .pin_bit_mask = 1ULL << CONFIG_WEARABLE_CHARGE_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&led);
#endif

#if CONFIG_WEARABLE_VBUS_SENSE_GPIO >= 0
    gpio_config_t pwr = {
        .pin_bit_mask = 1ULL << CONFIG_WEARABLE_VBUS_SENSE_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&pwr);
#endif

    adc_oneshot_unit_init_cfg_t unit_cfg = {.unit_id = ADC_UNIT_1};
    if (adc_oneshot_new_unit(&unit_cfg, &s_adc) == ESP_OK) {
        adc_oneshot_chan_cfg_t chan_cfg = {
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };

        s_adc_ready =
            adc_oneshot_config_channel(
                s_adc,
                (adc_channel_t)CONFIG_WEARABLE_CHARGER_STAT_ADC_CHANNEL,
                &chan_cfg) == ESP_OK;

#if CONFIG_WEARABLE_BATTERY_VOLTAGE_ADC_CHANNEL >= 0
        if (s_adc_ready) {
            s_adc_ready =
                adc_oneshot_config_channel(
                    s_adc,
                    (adc_channel_t)CONFIG_WEARABLE_BATTERY_VOLTAGE_ADC_CHANNEL,
                    &chan_cfg) == ESP_OK;
        }
#endif
    }

    xTaskCreate(power_task, "power", 3072, NULL, 3, NULL);
}

uint16_t power_manager_battery_mv(void)
{
    return s_battery_mv;
}
