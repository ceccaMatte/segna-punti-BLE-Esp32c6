#pragma once

#include <stdbool.h>

#include "wearable_types.h"

#define WEARABLE_CONFIG_SCHEMA_VERSION 2u

void wearable_config_set_defaults(wearable_config_t *out);
bool wearable_config_is_valid(const wearable_config_t *config);
