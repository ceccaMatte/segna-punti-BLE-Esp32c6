#pragma once

#include "esp_err.h"

#define WEARABLE_CONFIG_AP_SSID "wearable_config_eps32_c3"
#define WEARABLE_CONFIG_AP_URL  "http://192.168.4.1/"

esp_err_t config_ap_start(void);
