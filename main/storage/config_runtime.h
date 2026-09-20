#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "wearable_types.h"

typedef void (*config_runtime_changed_cb_t)(const wearable_config_t *config,
                                            void *ctx);

esp_err_t config_runtime_init(wearable_config_t *config,
                              config_runtime_changed_cb_t changed_cb,
                              void *ctx);

bool config_runtime_snapshot(wearable_config_t *out);

esp_err_t config_runtime_apply_command(const uint8_t *command,
                                       size_t len);
