#pragma once

#include <stdbool.h>

#include "wearable_types.h"

#define WEARABLE_CONFIG_SCHEMA_VERSION 5u

/* Calibrated from physical-button timing captures on the current enclosure. */
#define WEARABLE_DEFAULT_MULTI_CLICK_GAP_MS 280u
#define WEARABLE_DEFAULT_LONG_PRESS_MS 450u
#define WEARABLE_DEFAULT_SEQUENCE_GAP_MS 300u
#define WEARABLE_DEFAULT_SIMULTANEOUS_WINDOW_MS 60u

void wearable_config_set_defaults(wearable_config_t *out);
bool wearable_config_is_valid(const wearable_config_t *config);
