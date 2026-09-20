#pragma once

#include "esp_err.h"
#include "wearable_types.h"

typedef void (*gesture_action_cb_t)(wearable_action_t action, void *ctx);

esp_err_t gesture_engine_init(const wearable_config_t *config,
                              gesture_action_cb_t cb,
                              void *ctx);
void gesture_engine_update_config(const wearable_config_t *config);
void gesture_engine_feed(wearable_token_t token);
void gesture_engine_tick(void);
void gesture_engine_reset(void);
