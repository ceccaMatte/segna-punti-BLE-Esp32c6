#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "wearable_types.h"

esp_err_t sound_manager_start(const wearable_config_t *config);
void sound_manager_update_config(const wearable_config_t *config);

void sound_manager_play_action(wearable_action_t action);
void sound_manager_play_system(wearable_system_sound_t sound);

/*
 * Enqueues one atomic feedback pattern: action confirmation followed by the
 * highest-priority GAME/SET/MATCH melody described by transition_flags.
 */
void sound_manager_play_ack_feedback(wearable_action_t action,
                                     uint8_t transition_flags);
