#pragma once

#include "wearable_types.h"

void sound_manager_start(const wearable_config_t *config);
void sound_manager_update_config(const wearable_config_t *config);
void sound_manager_play_action(wearable_action_t action);
void sound_manager_play_system(wearable_system_sound_t sound);
