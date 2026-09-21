#include "sound_manager.h"

#include <string.h>

#include "driver/ledc.h"
#include "board_pins.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define SOUND_QUEUE_LEN 12u
#define SOUND_MAX_STEPS 6u

typedef struct {
    uint16_t frequency_hz;
    uint16_t duty_permille;
    uint16_t duration_ms;
    uint16_t gap_ms;
} sound_step_t;

typedef struct {
    uint8_t count;
    sound_step_t steps[SOUND_MAX_STEPS];
} sound_pattern_t;

static QueueHandle_t s_queue;
static wearable_tone_t s_action_sounds[WEARABLE_ACTION_SOUND_COUNT];
static portMUX_TYPE s_config_lock = portMUX_INITIALIZER_UNLOCKED;

static void silence(void)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void play_step(const sound_step_t *step)
{
    if (step->frequency_hz == 0u || step->duration_ms == 0u) {
        return;
    }

    if (ledc_set_freq(LEDC_LOW_SPEED_MODE,
                      LEDC_TIMER_0,
                      step->frequency_hz) == 0u) {
        return;
    }

    const uint32_t duty =
        ((1u << 10) - 1u) * step->duty_permille / 1000u;

    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    vTaskDelay(pdMS_TO_TICKS(step->duration_ms));
    silence();

    if (step->gap_ms != 0u) {
        vTaskDelay(pdMS_TO_TICKS(step->gap_ms));
    }
}

static sound_pattern_t system_pattern(wearable_system_sound_t sound)
{
    switch (sound) {
    case WEARABLE_SYS_PAIRING_STARTED:
        return (sound_pattern_t){
            3, {{1200,350,70,70},{1600,350,70,70},{2100,350,90,0}}
        };
    case WEARABLE_SYS_PAIRING_SUCCESS:
        return (sound_pattern_t){
            2, {{1700,350,80,50},{2500,350,120,0}}
        };
    case WEARABLE_SYS_ERROR:
        return (sound_pattern_t){
            2, {{500,450,110,60},{380,450,160,0}}
        };
    case WEARABLE_SYS_LOW_BATTERY:
        return (sound_pattern_t){
            2, {{700,300,90,70},{700,300,90,0}}
        };
    case WEARABLE_SYS_GAME_END:
        return (sound_pattern_t){
            2, {{1600,320,80,40},{2100,320,100,0}}
        };
    case WEARABLE_SYS_SET_END:
        return (sound_pattern_t){
            3, {{1400,320,70,40},{1800,320,70,40},{2400,320,120,0}}
        };
    case WEARABLE_SYS_MATCH_END:
        return (sound_pattern_t){
            4, {{1200,350,80,35},{1700,350,80,35},
                {2200,350,80,35},{2900,350,180,0}}
        };
    default:
        return (sound_pattern_t){0};
    }
}

static wearable_tone_t action_tone(wearable_action_t action)
{
    wearable_tone_t tone = {0};

    if (action >= WEARABLE_ACTION_SOUND_COUNT) {
        return tone;
    }

    portENTER_CRITICAL(&s_config_lock);
    tone = s_action_sounds[action];
    portEXIT_CRITICAL(&s_config_lock);
    return tone;
}

static void enqueue_pattern(const sound_pattern_t *pattern)
{
    if (s_queue == NULL || pattern == NULL || pattern->count == 0u) {
        return;
    }

    /*
     * Audio must never block NimBLE or button tasks. The queue is deliberately
     * long enough for short bursts; if saturated, the newest non-critical
     * feedback is dropped instead of stalling the radio stack.
     */
    xQueueSend(s_queue, pattern, 0);
}

static void sound_task(void *arg)
{
    (void)arg;

    sound_pattern_t pattern;
    for (;;) {
        if (xQueueReceive(s_queue, &pattern, portMAX_DELAY) == pdTRUE) {
            for (uint8_t i = 0; i < pattern.count; ++i) {
                play_step(&pattern.steps[i]);
            }
        }
    }
}

esp_err_t sound_manager_start(const wearable_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    sound_manager_update_config(config);

    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 2000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        return err;
    }

    ledc_channel_config_t channel = {
        .gpio_num = BOARD_GPIO_BUZZER,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    err = ledc_channel_config(&channel);
    if (err != ESP_OK) {
        return err;
    }

    s_queue = xQueueCreate(SOUND_QUEUE_LEN, sizeof(sound_pattern_t));
    if (s_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(sound_task, "sound", 3072, NULL, 4, NULL) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void sound_manager_update_config(const wearable_config_t *config)
{
    if (config == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_config_lock);
    memcpy(s_action_sounds,
           config->action_sounds,
           sizeof(s_action_sounds));
    portEXIT_CRITICAL(&s_config_lock);
}

void sound_manager_play_action(wearable_action_t action)
{
    const wearable_tone_t tone = action_tone(action);
    if (tone.duration_ms == 0u) {
        return;
    }

    const sound_pattern_t pattern = {
        .count = 1,
        .steps = {{
            tone.frequency_hz,
            tone.duty_permille,
            tone.duration_ms,
            0,
        }},
    };
    enqueue_pattern(&pattern);
}

void sound_manager_play_system(wearable_system_sound_t sound)
{
    const sound_pattern_t pattern = system_pattern(sound);
    enqueue_pattern(&pattern);
}

void sound_manager_play_ack_feedback(wearable_action_t action,
                                     uint8_t transition_flags)
{
    const wearable_tone_t tone = action_tone(action);
    if (tone.duration_ms == 0u) {
        return;
    }

    sound_pattern_t result = {
        .count = 1,
        .steps = {{
            tone.frequency_hz,
            tone.duty_permille,
            tone.duration_ms,
            0,
        }},
    };

    wearable_system_sound_t end_sound;
    bool has_end_sound = true;

    if ((transition_flags & WEARABLE_ACK_FLAG_MATCH_ENDED) != 0u) {
        end_sound = WEARABLE_SYS_MATCH_END;
    } else if ((transition_flags & WEARABLE_ACK_FLAG_SET_ENDED) != 0u) {
        end_sound = WEARABLE_SYS_SET_END;
    } else if ((transition_flags & WEARABLE_ACK_FLAG_GAME_ENDED) != 0u) {
        end_sound = WEARABLE_SYS_GAME_END;
    } else {
        has_end_sound = false;
    }

    if (has_end_sound) {
        sound_pattern_t ending = system_pattern(end_sound);
        result.steps[0].gap_ms = 40;

        for (uint8_t i = 0;
             i < ending.count && result.count < SOUND_MAX_STEPS;
             ++i) {
            result.steps[result.count++] = ending.steps[i];
        }
    }

    enqueue_pattern(&result);
}
