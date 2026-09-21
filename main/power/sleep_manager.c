#include "sleep_manager.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define SLEEP_WATCH_POLL_MS 500u

static const char *TAG = "sleep";

static int64_t s_last_button_activity_us;
static portMUX_TYPE s_activity_lock = portMUX_INITIALIZER_UNLOCKED;

static const int s_button_gpios[] = {
    BOARD_GPIO_BUTTON_A,
    BOARD_GPIO_BUTTON_B,
    BOARD_GPIO_BUTTON_MOMENT,
    BOARD_GPIO_BUTTON_UNDO,
};

static const char *const s_button_names[] = {
    "A",
    "B",
    "MOMENT",
    "UNDO",
};

static uint64_t button_gpio_mask(void)
{
    uint64_t mask = 0;

    for (size_t i = 0; i < sizeof(s_button_gpios) / sizeof(s_button_gpios[0]); ++i) {
        mask |= 1ULL << s_button_gpios[i];
    }

    return mask;
}

static int64_t last_activity_us(void)
{
    int64_t value;

    portENTER_CRITICAL(&s_activity_lock);
    value = s_last_button_activity_us;
    portEXIT_CRITICAL(&s_activity_lock);

    return value;
}

void sleep_manager_note_button_activity(void)
{
    const int64_t now = esp_timer_get_time();

    portENTER_CRITICAL(&s_activity_lock);
    s_last_button_activity_us = now;
    portEXIT_CRITICAL(&s_activity_lock);

    ESP_LOGD(TAG,
             "idle timer reset by button activity t_ms=%" PRId64,
             now / 1000);
}

static bool any_button_pressed(uint64_t *pressed_gpio_mask)
{
    uint64_t mask = 0;

    for (size_t i = 0; i < sizeof(s_button_gpios) / sizeof(s_button_gpios[0]); ++i) {
        const int level = gpio_get_level((gpio_num_t)s_button_gpios[i]);

#if CONFIG_WEARABLE_BUTTON_ACTIVE_LOW
        const bool pressed = level == 0;
#else
        const bool pressed = level != 0;
#endif

        if (pressed) {
            mask |= 1ULL << s_button_gpios[i];
        }
    }

    if (pressed_gpio_mask != NULL) {
        *pressed_gpio_mask = mask;
    }

    return mask != 0;
}

static void log_wakeup_banner(void)
{
    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    if (cause != ESP_SLEEP_WAKEUP_GPIO) {
        ESP_LOGI(TAG,
                 "boot is not a GPIO Deep-sleep wake (cause=%d)",
                 (int)cause);
        return;
    }

    const uint64_t wake_mask = esp_sleep_get_gpio_wakeup_status();

    ESP_LOGW(TAG, "============================================================");
    ESP_LOGW(TAG, "==============  WAKE FROM DEEP SLEEP  =====================");
    ESP_LOGW(TAG,
             "GPIO wake mask=0x%016" PRIx64,
             wake_mask);

    for (size_t i = 0; i < sizeof(s_button_gpios) / sizeof(s_button_gpios[0]); ++i) {
        if ((wake_mask & (1ULL << s_button_gpios[i])) != 0) {
            ESP_LOGW(TAG,
                     "wake source: button=%s gpio=%d",
                     s_button_names[i],
                     s_button_gpios[i]);
        }
    }

    ESP_LOGW(TAG, "============================================================");
}

static esp_err_t configure_button_wakeup(void)
{
    const uint64_t wake_mask = button_gpio_mask();

    for (size_t i = 0; i < sizeof(s_button_gpios) / sizeof(s_button_gpios[0]); ++i) {
        if (!esp_sleep_is_valid_wakeup_gpio((gpio_num_t)s_button_gpios[i])) {
            ESP_LOGE(TAG,
                     "GPIO%d (%s) is not valid as Deep-sleep wake source",
                     s_button_gpios[i],
                     s_button_names[i]);
            return ESP_ERR_INVALID_ARG;
        }
    }

#if ESP_IDF_VERSION_MAJOR >= 6
    return esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(
        wake_mask,
#if CONFIG_WEARABLE_BUTTON_ACTIVE_LOW
        ESP_GPIO_WAKEUP_GPIO_LOW
#else
        ESP_GPIO_WAKEUP_GPIO_HIGH
#endif
    );
#else
    return esp_deep_sleep_enable_gpio_wakeup(
        wake_mask,
#if CONFIG_WEARABLE_BUTTON_ACTIVE_LOW
        ESP_GPIO_WAKEUP_GPIO_LOW
#else
        ESP_GPIO_WAKEUP_GPIO_HIGH
#endif
    );
#endif
}

static esp_err_t hold_prototype_ground_low(void)
{
    /*
     * The flying prototype uses GPIO21 as the common LOW return for the
     * buttons. GPIO21 is a digital GPIO and would otherwise become high-Z in
     * Deep-sleep, preventing the buttons from pulling the wake GPIOs LOW.
     */
    esp_err_t err = gpio_set_level((gpio_num_t)BOARD_GPIO_GND_SINK, 0);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_hold_en((gpio_num_t)BOARD_GPIO_GND_SINK);
    if (err != ESP_OK) {
        return err;
    }

    gpio_deep_sleep_hold_en();
    return ESP_OK;
}

static void enter_deep_sleep(uint32_t idle_ms)
{
    uint64_t pressed_mask = 0;

    if (any_button_pressed(&pressed_mask)) {
        ESP_LOGW(TAG,
                 "Deep-sleep deferred: a button is still pressed gpio_mask=0x%016" PRIx64,
                 pressed_mask);
        sleep_manager_note_button_activity();
        return;
    }

    esp_err_t err = configure_button_wakeup();
    if (err != ESP_OK) {
        ESP_LOGE(TAG,
                 "cannot configure button wakeup: %s",
                 esp_err_to_name(err));
        sleep_manager_note_button_activity();
        return;
    }

    err = hold_prototype_ground_low();
    if (err != ESP_OK) {
        ESP_LOGE(TAG,
                 "cannot hold GPIO%d LOW for Deep-sleep: %s",
                 BOARD_GPIO_GND_SINK,
                 esp_err_to_name(err));
        sleep_manager_note_button_activity();
        return;
    }

    ESP_LOGW(TAG, "============================================================");
    ESP_LOGW(TAG, "===============  ENTERING DEEP SLEEP  =====================");
    ESP_LOGW(TAG,
             "reason: no physical button activity for %lu ms",
             (unsigned long)idle_ms);
    ESP_LOGW(TAG,
             "wake GPIOs: A=%d B=%d MOMENT=%d UNDO=%d, trigger=%s",
             BOARD_GPIO_BUTTON_A,
             BOARD_GPIO_BUTTON_B,
             BOARD_GPIO_BUTTON_MOMENT,
             BOARD_GPIO_BUTTON_UNDO,
#if CONFIG_WEARABLE_BUTTON_ACTIVE_LOW
             "LOW"
#else
             "HIGH"
#endif
    );
    ESP_LOGW(TAG,
             "GPIO%d is held LOW during Deep-sleep for prototype button return",
             BOARD_GPIO_GND_SINK);
    ESP_LOGW(TAG, "============================================================");

    /*
     * Give the serial monitor enough time to drain the banner before the USB
     * peripheral disappears in Deep-sleep.
     */
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));
    fflush(stdout);

    esp_deep_sleep_start();

    /* esp_deep_sleep_start() does not return. */
}

static void sleep_watch_task(void *arg)
{
    (void)arg;

    const int64_t timeout_us =
        (int64_t)CONFIG_WEARABLE_IDLE_SLEEP_MS * 1000LL;

    for (;;) {
        const int64_t now = esp_timer_get_time();
        const int64_t idle_us = now - last_activity_us();

        if (idle_us >= timeout_us) {
            enter_deep_sleep((uint32_t)(idle_us / 1000LL));
        }

        vTaskDelay(pdMS_TO_TICKS(SLEEP_WATCH_POLL_MS));
    }
}

esp_err_t sleep_manager_start(void)
{
    log_wakeup_banner();

    for (size_t i = 0; i < sizeof(s_button_gpios) / sizeof(s_button_gpios[0]); ++i) {
        if (!esp_sleep_is_valid_wakeup_gpio((gpio_num_t)s_button_gpios[i])) {
            ESP_LOGE(TAG,
                     "Deep-sleep disabled: GPIO%d (%s) cannot wake ESP32-C3",
                     s_button_gpios[i],
                     s_button_names[i]);
            return ESP_ERR_INVALID_ARG;
        }
    }

    sleep_manager_note_button_activity();

    ESP_LOGI(TAG,
             "Deep-sleep inactivity timeout=%u ms (%.1f min)",
             (unsigned)CONFIG_WEARABLE_IDLE_SLEEP_MS,
             (double)CONFIG_WEARABLE_IDLE_SLEEP_MS / 60000.0);

    if (xTaskCreate(sleep_watch_task,
                    "sleep_watch",
                    3072,
                    NULL,
                    2,
                    NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
