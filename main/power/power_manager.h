#pragma once

#include <stdint.h>

typedef enum {
    POWER_EVENT_LOW_BATTERY = 0,
    POWER_EVENT_CHARGING,
    POWER_EVENT_FULL,
} power_event_t;

typedef void (*power_event_cb_t)(power_event_t event, void *ctx);

void power_manager_start(power_event_cb_t cb, void *ctx);
uint16_t power_manager_battery_mv(void);
