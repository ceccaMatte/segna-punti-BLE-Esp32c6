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

/*
 * Logical button indices are intentionally stable because they are serialized
 * inside gesture tokens:
 *   0 = physical A button
 *   1 = physical B button
 *   2 = physical Moment button
 *   3 = physical Undo button
 *
 * The action mapping is still fully configurable; these names only identify
 * the physical keys printed on the enclosure / PCB.
 */
static const int s_pins[WEARABLE_BUTTON_COUNT] = {
    CONFIG_WEARABLE_BUTTON_A_GPIO,
    CONFIG_WEARABLE_BUTTON_B_GPIO,
    CONFIG_WEARABLE_BUTTON_MOMENT_GPIO,
    CONFIG_WEARABLE_BUTTON_UNDO_GPIO,
};

static button_state_t s_state[WEARABLE_BUTTON_COUNT];
static wearable_config_t s_config;
static button_primitive_cb_t s_cb;
static void *s_cb_ctx;

static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
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
        uint32_t now = now_ms();

        for (uint8_t i = 0; i < WEARABLE_BUTTON_COUNT; ++i) {
            button_state_t *st = &s_state[i];
            bool raw = is_pressed(s_pins[i]);

            if (raw != st->raw) {
                st->raw = raw;
                st->raw_since = now;
            }

            if (raw != st->stable && (uint32_t)(now - st->raw_since) >= DEBOUNCE_MS) {
                st->stable = raw;

                if (raw) {
                    st->pressed_since = now;
                } else {
                    uint32_t held = now - st->pressed_since;
                    if (held >= s_config.long_press_ms) {
                        st->click_count = 0;
                        st->click_deadline = 0;
                        emit(i, WEARABLE_PRIMITIVE_LONG);
                    } else {
                        if (st->click_count < 3u) {
                            st->click_count++;
                        }
                        if (st->click_count >= 3u) {
                            emit(i, WEARABLE_PRIMITIVE_TRIPLE);
                            st->click_count = 0;
                            st->click_deadline = 0;
                        } else {
                            st->click_deadline = now + s_config.multi_click_gap_ms;
                        }
                    }
                }
            }

            if (st->click_count > 0u && st->click_deadline != 0u &&
                (int32_t)(now - st->click_deadline) >= 0) {
                emit(i, st->click_count == 1u ? WEARABLE_PRIMITIVE_CLICK
                                              : WEARABLE_PRIMITIVE_DOUBLE);
                st->click_count = 0;
                st->click_deadline = 0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

void button_manager_start(const wearable_config_t *config, button_primitive_cb_t cb, void *ctx)
{
    s_config = *config;
    s_cb = cb;
    s_cb_ctx = ctx;
    memset(s_state, 0, sizeof(s_state));

    for (uint8_t i = 0; i < WEARABLE_BUTTON_COUNT; ++i) {
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << s_pins[i],
            .mode = GPIO_MODE_INPUT,
#if CONFIG_WEARABLE_BUTTON_ACTIVE_LOW
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
#else
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
#endif
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&cfg);
        s_state[i].raw = is_pressed(s_pins[i]);
        s_state[i].stable = s_state[i].raw;
    }

    xTaskCreate(button_task, "buttons", 3072, NULL, 6, NULL);
}

void button_manager_update_config(const wearable_config_t *config)
{
    s_config.multi_click_gap_ms = config->multi_click_gap_ms;
    s_config.long_press_ms = config->long_press_ms;
}
