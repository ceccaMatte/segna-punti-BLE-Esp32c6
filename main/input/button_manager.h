#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "wearable_types.h"

typedef void (*button_primitive_cb_t)(uint8_t token, void *ctx);

esp_err_t button_manager_start(const wearable_config_t *config,
                               button_primitive_cb_t cb,
                               void *ctx);
void button_manager_update_config(const wearable_config_t *config);
