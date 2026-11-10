#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#define PLAYMAKER_DISCOVERY_PORT 37020
#define PLAYMAKER_DISCOVERY_MAX 512
void playmaker_device_id(char out[13]);
esp_err_t playmaker_discovery_start(void);
