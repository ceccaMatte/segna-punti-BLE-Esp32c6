#include "button_manager.h"

#include <string.h>

#include "driver/gpio.h"
#include "board_pins.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define DEBOUNCE_MS 25u
#define POLL_MS 5u

static const char *TAG = "buttons";

typedef struct {
    bool raw;
    bool stable;
    uint32_t raw_since;
    int64_t pressed_at_us;
} button_state_t;

typedef struct {
    bool collecting;
    bool finalized;
    bool long_emitted;
    bool multi_started_in_time;
    uint8_t mask;
    uint32_t first_press_ms;
    uint32_t collect_deadline_ms;
} press_group_t;

typedef struct {
    uint8_t mask;
    uint8_t count;
    uint32_t deadline_ms;
} click_accumulator_t;

static const int s_pins[WEARABLE_BUTTON_COUNT] = {
    BOARD_GPIO_BUTTON_A,
    BOARD_GPIO_BUTTON_B,
    BOARD_GPIO_BUTTON_MOMENT,
    BOARD_GPIO_BUTTON_UNDO,
};

static button_state_t s_state[WEARABLE_BUTTON_COUNT];
static button_primitive_cb_t s_cb;
static void *s_cb_ctx;

static uint16_t s_multi_click_gap_ms;
static uint16_t s_long_press_ms;
static uint16_t s_simultaneous_window_ms;
static portMUX_TYPE s_timing_lock = portMUX_INITIALIZER_UNLOCKED;

static press_group_t s_group;
static click_accumulator_t s_clicks;

static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static const char *button_name(uint8_t button)
{
    static const char *names[WEARABLE_BUTTON_COUNT] = {
        "A", "B", "MOMENT", "UNDO"
    };

    return button < WEARABLE_BUTTON_COUNT ? names[button] : "?";
}

static const char *primitive_name(wearable_primitive_t primitive)
{
    switch (primitive) {
    case WEARABLE_PRIMITIVE_CLICK: return "click";
    case WEARABLE_PRIMITIVE_DOUBLE: return "double";
    case WEARABLE_PRIMITIVE_TRIPLE: return "triple";
    case WEARABLE_PRIMITIVE_LONG: return "long";
    default: return "?";
    }
}

static void timing_snapshot(uint16_t *multi_click_gap_ms,
                            uint16_t *long_press_ms,
                            uint16_t *simultaneous_window_ms)
{
    portENTER_CRITICAL(&s_timing_lock);
    *multi_click_gap_ms = s_multi_click_gap_ms;
    *long_press_ms = s_long_press_ms;
    *simultaneous_window_ms = s_simultaneous_window_ms;
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

static uint8_t stable_pressed_mask(void)
{
    uint8_t mask = 0;
    for (uint8_t i = 0; i < WEARABLE_BUTTON_COUNT; ++i) {
        if (s_state[i].stable) {
            mask |= (uint8_t)(1u << i);
        }
    }
    return mask;
}

static void emit_token(wearable_token_t token)
{
    if (s_cb == NULL) {
        return;
    }

    ESP_LOGI(TAG,
             "---[EMIT]--- type=%s mask=0x%02x token=0x%04x",
             primitive_name(wearable_token_primitive(token)),
             (unsigned)wearable_token_button_mask(token),
             (unsigned)token);
    s_cb(token, s_cb_ctx);
}

static void emit_mask(uint8_t mask, wearable_primitive_t primitive)
{
    emit_token(wearable_token_from_mask(mask, primitive));
}

static void reset_group(void)
{
    memset(&s_group, 0, sizeof(s_group));
}

static void flush_clicks(void)
{
    if (s_clicks.count == 0u || s_clicks.mask == 0u) {
        memset(&s_clicks, 0, sizeof(s_clicks));
        return;
    }

    const wearable_primitive_t primitive =
        s_clicks.count == 1u
            ? WEARABLE_PRIMITIVE_CLICK
            : WEARABLE_PRIMITIVE_DOUBLE;

    emit_mask(s_clicks.mask, primitive);
    memset(&s_clicks, 0, sizeof(s_clicks));
}

static void register_short_cycle(uint8_t mask,
                                 uint32_t now,
                                 uint16_t multi_click_gap_ms,
                                 bool started_in_time)
{
    /*
     * The multi-click gap is defined from RELEASE(n) to PRESS(n+1), not to
     * RELEASE(n+1). Once the next press starts inside the window we must keep
     * the series alive while that press is held; otherwise a slightly slower
     * third click is incorrectly emitted as DOUBLE + CLICK.
     */
    if (s_clicks.count != 0u &&
        (s_clicks.mask != mask ||
         (!started_in_time &&
          (int32_t)(now - s_clicks.deadline_ms) >= 0))) {
        flush_clicks();
    }

    if (s_clicks.count == 0u) {
        s_clicks.mask = mask;
    }

    s_clicks.count++;

    if (s_clicks.count >= 3u) {
        emit_mask(mask, WEARABLE_PRIMITIVE_TRIPLE);
        memset(&s_clicks, 0, sizeof(s_clicks));
        return;
    }

    s_clicks.deadline_ms = now + multi_click_gap_ms;

    ESP_LOGD(TAG,
             "short cycle mask=0x%02x count=%u next_press_deadline=%lu",
             (unsigned)mask,
             (unsigned)s_clicks.count,
             (unsigned long)s_clicks.deadline_ms);
}

static void start_or_extend_group(uint8_t button,
                                  uint32_t now,
                                  uint16_t simultaneous_window_ms)
{
    const uint8_t bit = (uint8_t)(1u << button);

    if (!s_group.collecting && !s_group.finalized) {
        s_group.collecting = true;
        s_group.mask = bit;
        s_group.first_press_ms = now;
        s_group.collect_deadline_ms = now + simultaneous_window_ms;
        s_group.multi_started_in_time =
            s_clicks.count != 0u &&
            (int32_t)(now - s_clicks.deadline_ms) < 0;

        ESP_LOGD(TAG,
                 "group open mask=0x%02x window=%u ms multi_pending=%u started_in_time=%d",
                 (unsigned)s_group.mask,
                 (unsigned)simultaneous_window_ms,
                 (unsigned)s_clicks.count,
                 (int)s_group.multi_started_in_time);
        return;
    }

    if (s_group.collecting) {
        s_group.mask |= bit;
        ESP_LOGD(TAG,
                 "group extend mask=0x%02x",
                 (unsigned)s_group.mask);
        return;
    }

    ESP_LOGW(TAG,
             "button=%u pressed while group mask=0x%02x already finalized; ignored until release",
             (unsigned)button,
             (unsigned)s_group.mask);
}

static void finalize_group_if_due(uint32_t now)
{
    if (!s_group.collecting ||
        (int32_t)(now - s_group.collect_deadline_ms) < 0) {
        return;
    }

    s_group.collecting = false;
    s_group.finalized = true;

    /*
     * A press that started inside the previous multi-click window only belongs
     * to that series if the completed simultaneous mask is identical. If the
     * user pressed a different button/group, close the old click immediately
     * instead of delaying it until this unrelated press is released.
     */
    if (s_group.multi_started_in_time &&
        s_clicks.count != 0u &&
        s_clicks.mask != s_group.mask) {
        ESP_LOGD(TAG,
                 "multi continuation mask mismatch pending=0x%02x new=0x%02x",
                 (unsigned)s_clicks.mask,
                 (unsigned)s_group.mask);
        flush_clicks();
        s_group.multi_started_in_time = false;
    }

    ESP_LOGI(TAG,
             "group finalized mask=0x%02x buttons=%u",
             (unsigned)s_group.mask,
             (unsigned)wearable_popcount4(s_group.mask));
}

static void process_finalized_group(uint32_t now,
                                    uint16_t multi_click_gap_ms,
                                    uint16_t long_press_ms)
{
    if (!s_group.finalized || s_group.mask == 0u) {
        return;
    }

    const uint8_t pressed =
        (uint8_t)(stable_pressed_mask() & s_group.mask);
    const uint32_t held_ms = now - s_group.first_press_ms;

    if (!s_group.long_emitted &&
        pressed == s_group.mask &&
        held_ms >= long_press_ms) {
        flush_clicks();
        emit_mask(s_group.mask, WEARABLE_PRIMITIVE_LONG);
        s_group.long_emitted = true;

        ESP_LOGI(TAG,
                 "long group mask=0x%02x held=%lu ms",
                 (unsigned)s_group.mask,
                 (unsigned long)held_ms);
    }

    if (pressed != 0u) {
        return;
    }

    const uint8_t mask = s_group.mask;
    if (!s_group.long_emitted) {
        register_short_cycle(mask,
                             now,
                             multi_click_gap_ms,
                             s_group.multi_started_in_time);
    }

    ESP_LOGD(TAG,
             "group complete mask=0x%02x held=%lu ms long=%d",
             (unsigned)mask,
             (unsigned long)held_ms,
             (int)s_group.long_emitted);
    reset_group();
}

static void button_task(void *arg)
{
    (void)arg;

    for (;;) {
        const uint32_t now = now_ms();
        uint16_t multi_click_gap_ms;
        uint16_t long_press_ms;
        uint16_t simultaneous_window_ms;

        timing_snapshot(&multi_click_gap_ms,
                        &long_press_ms,
                        &simultaneous_window_ms);

        for (uint8_t i = 0; i < WEARABLE_BUTTON_COUNT; ++i) {
            button_state_t *state = &s_state[i];
            const bool raw = is_pressed(s_pins[i]);

            if (raw != state->raw) {
                state->raw = raw;
                state->raw_since = now;

                ESP_LOGI(TAG,
                         "RAW button=%s gpio=%d edge=%s level=%d t_us=%lld",
                         button_name(i),
                         s_pins[i],
                         raw ? "PRESS" : "RELEASE",
                         gpio_get_level((gpio_num_t)s_pins[i]),
                         (long long)esp_timer_get_time());
            }

            if (raw != state->stable &&
                (uint32_t)(now - state->raw_since) >= DEBOUNCE_MS) {
                state->stable = raw;

                const int64_t edge_us = esp_timer_get_time();

                if (raw) {
                    state->pressed_at_us = edge_us;

                    ESP_LOGI(TAG,
                             "TIMING button=%s gpio=%d edge=PRESS t_us=%lld t_ms=%lld debounce_ms=%u",
                             button_name(i),
                             s_pins[i],
                             (long long)edge_us,
                             (long long)(edge_us / 1000),
                             (unsigned)DEBOUNCE_MS);

                    start_or_extend_group(i,
                                          now,
                                          simultaneous_window_ms);
                } else {
                    const int64_t held_us =
                        state->pressed_at_us > 0
                            ? edge_us - state->pressed_at_us
                            : 0;

                    ESP_LOGI(TAG,
                             "TIMING button=%s gpio=%d edge=RELEASE t_us=%lld t_ms=%lld held_us=%lld held_ms=%.3f debounce_ms=%u",
                             button_name(i),
                             s_pins[i],
                             (long long)edge_us,
                             (long long)(edge_us / 1000),
                             (long long)held_us,
                             (double)held_us / 1000.0,
                             (unsigned)DEBOUNCE_MS);
                }
            }
        }

        finalize_group_if_due(now);
        process_finalized_group(now,
                                multi_click_gap_ms,
                                long_press_ms);

        if (s_clicks.count != 0u &&
            (int32_t)(now - s_clicks.deadline_ms) >= 0) {
            const bool next_press_started_in_time =
                (s_group.collecting || s_group.finalized) &&
                s_group.multi_started_in_time;

            if (!next_press_started_in_time) {
                flush_clicks();
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
    reset_group();
    memset(&s_clicks, 0, sizeof(s_clicks));
    button_manager_update_config(config);

    const uint32_t now = now_ms();

    ESP_LOGI(TAG,
             "pinmap A=GPIO%d B=GPIO%d MOMENT=GPIO%d UNDO=GPIO%d active_low=%d",
             BOARD_GPIO_BUTTON_A,
             BOARD_GPIO_BUTTON_B,
             BOARD_GPIO_BUTTON_MOMENT,
             BOARD_GPIO_BUTTON_UNDO,
#if CONFIG_WEARABLE_BUTTON_ACTIVE_LOW
             1
#else
             0
#endif
    );

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
        s_state[i].pressed_at_us =
            s_state[i].stable ? esp_timer_get_time() : 0;

        ESP_LOGI(TAG,
                 "button=%s gpio=%d initial=%s level=%d",
                 button_name(i),
                 s_pins[i],
                 s_state[i].stable ? "pressed" : "released",
                 gpio_get_level((gpio_num_t)s_pins[i]));
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
    s_simultaneous_window_ms = config->simultaneous_window_ms;
    portEXIT_CRITICAL(&s_timing_lock);

    ESP_LOGI(TAG,
             "timings debounce=%u poll=%u multi=%u long=%u simultaneous=%u ms",
             (unsigned)DEBOUNCE_MS,
             (unsigned)POLL_MS,
             (unsigned)config->multi_click_gap_ms,
             (unsigned)config->long_press_ms,
             (unsigned)config->simultaneous_window_ms);
}
