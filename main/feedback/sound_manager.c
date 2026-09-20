#include "sound_manager.h"

#include <string.h>
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

typedef struct {
    uint16_t frequency_hz;
    uint16_t duty_permille;
    uint16_t duration_ms;
    uint16_t gap_ms;
} step_t;

typedef struct {
    uint8_t count;
    step_t steps[4];
} pattern_t;

static QueueHandle_t s_queue;
static wearable_tone_t s_action_sounds[WEARABLE_ACTION_SOUND_COUNT];

static void silence(void)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void play_step(const step_t *s)
{
    if (s->frequency_hz == 0 || s->duration_ms == 0) {
        return;
    }
    ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, s->frequency_hz);
    uint32_t duty = ((1u << 10) - 1u) * s->duty_permille / 1000u;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    vTaskDelay(pdMS_TO_TICKS(s->duration_ms));
    silence();
    if (s->gap_ms) {
        vTaskDelay(pdMS_TO_TICKS(s->gap_ms));
    }
}

static pattern_t system_pattern(wearable_system_sound_t sound)
{
    pattern_t p = {0};
    switch (sound) {
    case WEARABLE_SYS_PAIRING_STARTED:
        p = (pattern_t){3, {{1200,350,70,70},{1600,350,70,70},{2100,350,90,0}}}; break;
    case WEARABLE_SYS_PAIRING_SUCCESS:
        p = (pattern_t){2, {{1700,350,80,50},{2500,350,120,0}}}; break;
    case WEARABLE_SYS_ERROR:
        p = (pattern_t){2, {{500,450,110,60},{380,450,160,0}}}; break;
    case WEARABLE_SYS_LOW_BATTERY:
        p = (pattern_t){2, {{700,300,90,70},{700,300,90,0}}}; break;
    case WEARABLE_SYS_GAME_END:
        p = (pattern_t){2, {{1600,320,80,40},{2100,320,100,0}}}; break;
    case WEARABLE_SYS_SET_END:
        p = (pattern_t){3, {{1400,320,70,40},{1800,320,70,40},{2400,320,120,0}}}; break;
    case WEARABLE_SYS_MATCH_END:
        p = (pattern_t){4, {{1200,350,80,35},{1700,350,80,35},{2200,350,80,35},{2900,350,180,0}}}; break;
    default:
        break;
    }
    return p;
}

static void sound_task(void *arg)
{
    (void)arg;
    pattern_t pattern;
    for (;;) {
        if (xQueueReceive(s_queue, &pattern, portMAX_DELAY) == pdTRUE) {
            for (uint8_t i = 0; i < pattern.count; ++i) {
                play_step(&pattern.steps[i]);
            }
        }
    }
}

void sound_manager_start(const wearable_config_t *config)
{
    memcpy(s_action_sounds, config->action_sounds, sizeof(s_action_sounds));

    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 2000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t ch = {
        .gpio_num = CONFIG_WEARABLE_BUZZER_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&ch);

    s_queue = xQueueCreate(8, sizeof(pattern_t));
    xTaskCreate(sound_task, "sound", 3072, NULL, 4, NULL);
}

void sound_manager_update_config(const wearable_config_t *config)
{
    memcpy(s_action_sounds, config->action_sounds, sizeof(s_action_sounds));
}

void sound_manager_play_action(wearable_action_t action)
{
    if (action >= WEARABLE_ACTION_SOUND_COUNT || s_queue == NULL) {
        return;
    }
    wearable_tone_t tone = s_action_sounds[action];
    pattern_t p = {1, {{tone.frequency_hz, tone.duty_permille, tone.duration_ms, 0}}};
    xQueueSend(s_queue, &p, 0);
}

void sound_manager_play_system(wearable_system_sound_t sound)
{
    if (s_queue == NULL) {
        return;
    }
    pattern_t p = system_pattern(sound);
    if (p.count) {
        xQueueSend(s_queue, &p, 0);
    }
}
