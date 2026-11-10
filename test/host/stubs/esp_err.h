#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_NO_MEM 2
static inline const char *esp_err_to_name(int err) { (void)err; return "stub"; }
