#pragma once
#include "esp_http_server.h"
#include "cJSON.h"
esp_err_t receiver_start(bool lan_mode);
esp_err_t receiver_register(httpd_handle_t server);
