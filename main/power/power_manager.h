#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef enum {
    POWER_EVENT_LOW_BATTERY = 0,
    POWER_EVENT_CHARGING,
    POWER_EVENT_FULL,
} power_event_t;

typedef void (*power_event_cb_t)(power_event_t event, void *ctx);

esp_err_t power_manager_start(power_event_cb_t cb, void *ctx);

/*
 * Returns 0 when the current PCB has no dedicated Vbat measurement channel.
 * MCP73831 STAT is not a battery-voltage signal and is never reported here.
 */
uint16_t power_manager_battery_mv(void);
