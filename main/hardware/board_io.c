#include "board_io.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "board_io";

esp_err_t board_io_init(void)
{
    gpio_config_t input = {
        .pin_bit_mask = 1ULL << CONFIG_WEARABLE_AUX_INPUT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&input);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,
                 "GPIO%d input init failed: %s",
                 CONFIG_WEARABLE_AUX_INPUT_GPIO,
                 esp_err_to_name(err));
        return err;
    }

    gpio_config_t ground_sink = {
        .pin_bit_mask = 1ULL << CONFIG_WEARABLE_GND_SINK_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    err = gpio_config(&ground_sink);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,
                 "GPIO%d ground-sink init failed: %s",
                 CONFIG_WEARABLE_GND_SINK_GPIO,
                 esp_err_to_name(err));
        return err;
    }

    err = gpio_set_level((gpio_num_t)CONFIG_WEARABLE_GND_SINK_GPIO, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,
                 "GPIO%d could not be driven LOW: %s",
                 CONFIG_WEARABLE_GND_SINK_GPIO,
                 esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG,
             "prototype IO: GPIO%d=input level=%d, GPIO%d=LOW",
             CONFIG_WEARABLE_AUX_INPUT_GPIO,
             gpio_get_level((gpio_num_t)CONFIG_WEARABLE_AUX_INPUT_GPIO),
             CONFIG_WEARABLE_GND_SINK_GPIO);

    return ESP_OK;
}
