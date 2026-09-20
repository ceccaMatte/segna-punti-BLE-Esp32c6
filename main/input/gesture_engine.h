#pragma once

#include "wearable_types.h"

typedef void (*gesture_action_cb_t)(wearable_action_t action, void *ctx);

void gesture_engine_init(const wearable_config_t *config, gesture_action_cb_t cb, void *ctx);
void gesture_engine_update_config(const wearable_config_t *config);
void gesture_engine_feed(uint8_t token);
void gesture_engine_tick(void);
void gesture_engine_reset(void);
