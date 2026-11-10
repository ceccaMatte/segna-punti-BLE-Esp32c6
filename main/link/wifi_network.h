#pragma once
#include "esp_http_server.h"
esp_err_t wifi_network_start(void);
esp_err_t wifi_network_register(httpd_handle_t server);
