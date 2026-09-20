#include "button_manager.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define DEBOUNCE_MS 25u
#define POLL_MS 5u

static const char *TAG = "buttons";

typedef struct {
    bool raw;
    bool stable;
    bool suppress_release;
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
static uint16_t s_chord_window_ms;
static portMUX_TYPE s_timing_lock = portMUX_INITIALIZER_UNLOCKED;

static uint8_t s_chord_candidate_mask;
static uint32_t s_chord_deadline;

static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static const char *primitive_name(wearable_primitive_t primitive)
{
    switch (primitive) {
    case WEARABLE_PRIMITIVE_CLICK: return "click";
    case WEARABLE_PRIMITIVE_DOUBLE: return "double";
    case WEARABLE_PRIMITIVE_TRIPLE: return "triple";
    case WEARABLE_PRIMITIVE_LONG: return "long";
    case WEARABLE_PRIMITIVE_CHORD: return "chord";
    default: return "?";
    }
}

static void timing_snapshot(uint16_t *multi_click_gap_ms,
                            uint16_t *long_press_ms,
                            uint16_t *chord_window_ms)
{
    portENTER_CRITICAL(&s_timing_lock);
    *multi_click_gap_ms = s_multi_click_gap_ms;
    *long_press_ms = s_long_press_ms;
    *chord_window_ms = s_chord_window_ms;
    portEXIT_CRITICAL(&s_timing_lock);
}

static bool is_pressed(int gpio)
{
    const int level = gpio_get_level((gpio_num_t)gpio);
#if CONFIG_WEARABLE_BUTTON_ACTIVE_LOW
    return level == 0;
#else
    return level != 0;
#endif
}

static void emit_token(wearable_token_t token)
{
    if (s_cb != NULL) {
        ESP_LOGI(TAG,
                 "emit primitive=%s mask=0x%02x token=0x%04x",
                 primitive_name(wearable_token_primitive(token)),
                 (unsigned)wearable_token_button_mask(token),
                 (unsigned)token);
        s_cb(token, s_cb_ctx);
    }
}

static void emit_single(uint8_t button, wearable_primitive_t primitive)
{
    emit_token(wearable_token(button, primitive));
}

static void chord_candidate_add(uint8_t button,
                                uint32_t now,
                                uint16_t chord_window_ms)
{
    const uint8_t bit = (uint8_t)(1u << button);

    if (s_chord_candidate_mask == 0u) {
        s_chord_candidate_mask = bit;
        s_chord_deadline = now + chord_window_ms;
        ESP_LOGD(TAG,
                 "chord window open button=%u window=%u ms",
                 (unsigned)button,
                 (unsigned)chord_window_ms);
        return;
    }

    s_chord_candidate_mask |= bit;
    ESP_LOGD(TAG,
             "chord candidate mask=0x%02x",
             (unsigned)s_chord_candidate_mask);
}

static void chord_candidate_remove(uint8_t button)
{
    s_chord_candidate_mask &=
        (uint8_t)~(uint8_t)(1u << button);

    if (s_chord_candidate_mask == 0u) {
        s_chord_deadline = 0;
    }
}

static void finalize_chord_if_due(uint32_t now)
{
    if (s_chord_candidate_mask == 0u ||
        s_chord_deadline == 0u ||
        (int32_t)(now - s_chord_deadline) < 0) {
        return;
    }

    const uint8_t mask = s_chord_candidate_mask;
    s_chord_candidate_mask = 0;
    s_chord_deadline = 0;

    if (wearable_popcount4(mask) < 2u) {
        return;
    }

    /*
     * A chord consumes the individual gestures of every participating button.
     * Clicks are not emitted until the multi-click timeout, so clearing the
     * pending click state here also covers a button released just before the
     * chord window closed.
     */
    for (uint8_t i = 0; i < WEARABLE_BUTTON_COUNT; ++i) {
        if ((mask & (uint8_t)(1u << i)) == 0u) {
            continue;
        }

        s_state[i].click_count = 0;
        s_state[i].click_deadline = 0;
        if (s_state[i].stable) {
            s_state[i].suppress_release = true;
        }
    }

    ESP_LOGI(TAG,
             "chord recognized mask=0x%02x",
             (unsigned)mask);
    emit_token(wearable_token_from_mask(mask,
                                        WEARABLE_PRIMITIVE_CHORD));
}

static void button_task(void *arg)
{
    (void)arg;

    for (;;) {
        const uint32_t now = now_ms();
        uint16_t multi_click_gap_ms;
        uint16_t long_press_ms;
        uint16_t chord_window_ms;

        timing_snapshot(&multi_click_gap_ms,
                        &long_press_ms,
                        &chord_window_ms);

        /*
         * Finalize before processing releases in this iteration. If two
         * buttons were held through the whole chord window, a release sampled
         * on the deadline must not turn the chord into two single clicks.
         */
        finalize_chord_if_due(now);

        for (uint8_t i = 0; i < WEARABLE_BUTTON_COUNT; ++i) {
            button_state_t *state = &s_state[i];
            const bool raw = is_pressed(s_pins[i]);

            if (raw != state->raw) {
                state->raw = raw;
                state->raw_since = now;
            }

            if (raw != state->stable &&
                (uint32_t)(now - state->raw_since) >= DEBOUNCE_MS) {
                state->stable = raw;

                ESP_LOGD(TAG,
                         "button=%u stable=%s",
                         (unsigned)i,
                         raw ? "pressed" : "released");

                if (raw) {
                    state->pressed_since = now;
                    chord_candidate_add(i,
                                        now,
                                        chord_window_ms);
                } else {
                    chord_candidate_remove(i);

                    if (state->suppress_release) {
                        state->suppress_release = false;
                        state->click_count = 0;
                        state->click_deadline = 0;
                        continue;
                    }

                    const uint32_t held_ms =
                        now - state->pressed_since;

                    if (held_ms >= long_press_ms) {
                        state->click_count = 0;
                        state->click_deadline = 0;
                        emit_single(i,
                                    WEARABLE_PRIMITIVE_LONG);
                    } else {
                        if (state->click_count < 3u) {
                            state->click_count++;
                        }

                        if (state->click_count == 3u) {
                            emit_single(i,
                                        WEARABLE_PRIMITIVE_TRIPLE);
                            state->click_count = 0;
                            state->click_deadline = 0;
                        } else {
                            state->click_deadline =
                                now + multi_click_gap_ms;
                        }
                    }
                }
            }

            if (state->click_count > 0u &&
                state->click_deadline != 0u &&
                (int32_t)(now - state->click_deadline) >= 0) {
                emit_single(
                    i,
                    state->click_count == 1u
                        ? WEARABLE_PRIMITIVE_CLICK
                        : WEARABLE_PRIMITIVE_DOUBLE);
                state->click_count = 0;
                state->click_deadline = 0;
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
    s_chord_candidate_mask = 0;
    s_chord_deadline = 0;
    button_manager_update_config(config);

    const uint32_t now = now_ms();

    for (uint8_t i = 0; i < WEARABLE_BUTTON_COUNT; ++i) {
        gpio_config_t gpio = {
            .pin_bit_mask = 1ULL << s_pins[i],
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };

        const esp_err_t err = gpio_config(&gpio);
        if (err != ESP_OK) {
            ESP_LOGE(TAG,
                     "gpio init failed button=%u gpio=%d err=%s",
                     (unsigned)i,
                     s_pins[i],
                     esp_err_to_name(err));
            return err;
        }

        s_state[i].raw = is_pressed(s_pins[i]);
        s_state[i].stable = s_state[i].raw;
        s_state[i].raw_since = now;
        s_state[i].pressed_since = now;

        ESP_LOGI(TAG,
                 "button=%u gpio=%d initial=%s",
                 (unsigned)i,
                 s_pins[i],
                 s_state[i].stable ? "pressed" : "released");
    }

    if (xTaskCreate(button_task,
                    "buttons",
                    3072,
                    NULL,
                    6,
                    NULL) != pdPASS) {
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
    s_chord_window_ms = config->chord_window_ms;
    portEXIT_CRITICAL(&s_timing_lock);

    ESP_LOGI(TAG,
             "timings multi=%u long=%u chord=%u ms",
             (unsigned)config->multi_click_gap_ms,
             (unsigned)config->long_press_ms,
             (unsigned)config->chord_window_ms);
}
