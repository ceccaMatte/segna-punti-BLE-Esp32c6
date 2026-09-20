#include "power_manager.h"

#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static power_event_cb_t s_cb;
static void *s_ctx;
static uint16_t s_battery_mv;
static adc_oneshot_unit_handle_t s_adc;
static bool s_adc_ready;

static bool input_active(int gpio, bool active_high)
{
    if (gpio < 0) return false;
    int v = gpio_get_level((gpio_num_t)gpio);
    return active_high ? (v != 0) : (v == 0);
}

static uint16_t sample_battery(void)
{
#if CONFIG_WEARABLE_BATTERY_ADC_CHANNEL >= 0
    if (!s_adc_ready) return 0;
    int raw = 0;
    if (adc_oneshot_read(s_adc, (adc_channel_t)CONFIG_WEARABLE_BATTERY_ADC_CHANNEL, &raw) != ESP_OK) {
        return 0;
    }
    uint32_t mv = (uint32_t)raw * 3300u / 4095u;
    mv = mv * CONFIG_WEARABLE_BATTERY_DIVIDER_NUM / CONFIG_WEARABLE_BATTERY_DIVIDER_DEN;
    return (uint16_t)mv;
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
    uint32_t elapsed = 0;
    uint32_t low_elapsed = 0;
    bool blink = false;
    bool last_charging = false;
    bool last_full = false;

    for (;;) {
        bool charging = input_active(CONFIG_WEARABLE_CHARGING_GPIO,
#if CONFIG_WEARABLE_CHARGING_ACTIVE_LOW
                                     false
#else
                                     true
#endif
                                     );
        bool power_present = input_active(CONFIG_WEARABLE_POWER_PRESENT_GPIO,
#if CONFIG_WEARABLE_POWER_PRESENT_ACTIVE_HIGH
                                          true
#else
                                          false
#endif
                                          );
        bool full = CONFIG_WEARABLE_POWER_PRESENT_GPIO >= 0 && power_present && !charging;

        if (charging) {
            blink = !blink;
            set_charge_led(blink);
        } else if (full) {
            set_charge_led(true);
        } else {
            set_charge_led(false);
        }

        if (charging && !last_charging && s_cb) s_cb(POWER_EVENT_CHARGING, s_ctx);
        if (full && !last_full && s_cb) s_cb(POWER_EVENT_FULL, s_ctx);
        last_charging = charging;
        last_full = full;

        elapsed += 500;
        low_elapsed += 500;

        if (elapsed >= 5000) {
            elapsed = 0;
            s_battery_mv = sample_battery();
        }

        if (s_battery_mv > 0 && s_battery_mv <= CONFIG_WEARABLE_LOW_BATTERY_MV &&
            !charging && low_elapsed >= CONFIG_WEARABLE_LOW_BATTERY_BEEP_MS) {
            low_elapsed = 0;
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

#if CONFIG_WEARABLE_CHARGING_GPIO >= 0
    gpio_config_t chrg = {
        .pin_bit_mask = 1ULL << CONFIG_WEARABLE_CHARGING_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&chrg);
#endif

#if CONFIG_WEARABLE_POWER_PRESENT_GPIO >= 0
    gpio_config_t pwr = {
        .pin_bit_mask = 1ULL << CONFIG_WEARABLE_POWER_PRESENT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&pwr);
#endif

#if CONFIG_WEARABLE_BATTERY_ADC_CHANNEL >= 0
    adc_oneshot_unit_init_cfg_t unit_cfg = {.unit_id = ADC_UNIT_1};
    if (adc_oneshot_new_unit(&unit_cfg, &s_adc) == ESP_OK) {
        adc_oneshot_chan_cfg_t chan_cfg = {
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        s_adc_ready = adc_oneshot_config_channel(
            s_adc, (adc_channel_t)CONFIG_WEARABLE_BATTERY_ADC_CHANNEL, &chan_cfg) == ESP_OK;
    }
#endif

    xTaskCreate(power_task, "power", 3072, NULL, 3, NULL);
}

uint16_t power_manager_battery_mv(void)
{
    return s_battery_mv;
}
