#include "button_manager.h"

#include <string.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define DEBOUNCE_MS 25u
#define POLL_MS 5u

typedef struct {
    bool raw;
    bool stable;
    uint32_t raw_since;
    uint32_t pressed_since;
    uint32_t click_deadline;
    uint8_t click_count;
} button_state_t;

static const int s_pins[WEARABLE_BUTTON_COUNT] = {
    CONFIG_WEARABLE_BUTTON_A_GPIO,
    CONFIG_WEARABLE_BUTTON_B_GPIO,
    CONFIG_WEARABLE_BUTTON_MOMENT_GPIO,
    CONFIG_WEARABLE_BUTTON_UNDO_GPIO,
};

static button_state_t s_state[WEARABLE_BUTTON_COUNT];
static button_primitive_cb_t s_cb;
static void *s_cb_ctx;
static uint16_t s_multi_click_gap_ms;
static uint16_t s_long_press_ms;
static portMUX_TYPE s_timing_lock = portMUX_INITIALIZER_UNLOCKED;

static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void timing_snapshot(uint16_t *multi_click_gap_ms, uint16_t *long_press_ms)
{
    portENTER_CRITICAL(&s_timing_lock);
    *multi_click_gap_ms = s_multi_click_gap_ms;
    *long_press_ms = s_long_press_ms;
    portEXIT_CRITICAL(&s_timing_lock);
}

static bool is_pressed(int gpio)
{
    int level = gpio_get_level((gpio_num_t)gpio);
#if CONFIG_WEARABLE_BUTTON_ACTIVE_LOW
    return level == 0;
#else
    return level != 0;
#endif
}

static void emit(uint8_t button, wearable_primitive_t primitive)
{
    if (s_cb != NULL) {
        s_cb(wearable_token(button, primitive), s_cb_ctx);
    }
}

static void button_task(void *arg)
{
    (void)arg;

    for (;;) {
        const uint32_t now = now_ms();
        uint16_t multi_click_gap_ms;
        uint16_t long_press_ms;
        timing_snapshot(&multi_click_gap_ms, &long_press_ms);

        for (uint8_t i = 0; i < WEARABLE_BUTTON_COUNT; ++i) {
            button_state_t *st = &s_state[i];
            const bool raw = is_pressed(s_pins[i]);

            if (raw != st->raw) {
                st->raw = raw;
                st->raw_since = now;
            }

            if (raw != st->stable &&
                (uint32_t)(now - st->raw_since) >= DEBOUNCE_MS) {
                st->stable = raw;

                if (raw) {
                    st->pressed_since = now;
                } else {
                    const uint32_t held_ms = now - st->pressed_since;

                    if (held_ms >= long_press_ms) {
                        st->click_count = 0;
                        st->click_deadline = 0;
                        emit(i, WEARABLE_PRIMITIVE_LONG);
                    } else {
                        if (st->click_count < 3u) {
                            st->click_count++;
                        }

                        if (st->click_count == 3u) {
                            emit(i, WEARABLE_PRIMITIVE_TRIPLE);
                            st->click_count = 0;
                            st->click_deadline = 0;
                        } else {
                            st->click_deadline = now + multi_click_gap_ms;
                        }
                    }
                }
            }

            if (st->click_count > 0u &&
                st->click_deadline != 0u &&
                (int32_t)(now - st->click_deadline) >= 0) {
                emit(i,
                     st->click_count == 1u
                         ? WEARABLE_PRIMITIVE_CLICK
                         : WEARABLE_PRIMITIVE_DOUBLE);
                st->click_count = 0;
                st->click_deadline = 0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

esp_err_t button_manager_start(const wearable_config_t *config,
                               button_primitive_cb_t cb,
                               void *ctx)
{
    if (config == NULL || cb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_cb = cb;
    s_cb_ctx = ctx;
    memset(s_state, 0, sizeof(s_state));
    button_manager_update_config(config);

    const uint32_t now = now_ms();

    for (uint8_t i = 0; i < WEARABLE_BUTTON_COUNT; ++i) {
        /*
         * The PCB already has 100 kOhm external pull-ups. Do not enable the
         * internal pull-up as well: it is unnecessary and increases current
         * while a button is held.
         */
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << s_pins[i],
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };

        esp_err_t err = gpio_config(&cfg);
        if (err != ESP_OK) {
            return err;
        }

        s_state[i].raw = is_pressed(s_pins[i]);
        s_state[i].stable = s_state[i].raw;
        s_state[i].raw_since = now;
        s_state[i].pressed_since = now;
    }

    if (xTaskCreate(button_task, "buttons", 3072, NULL, 6, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void button_manager_update_config(const wearable_config_t *config)
{
    if (config == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_timing_lock);
    s_multi_click_gap_ms = config->multi_click_gap_ms;
    s_long_press_ms = config->long_press_ms;
    portEXIT_CRITICAL(&s_timing_lock);
}
