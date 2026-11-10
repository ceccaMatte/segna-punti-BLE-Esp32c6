#pragma once
#include <stdio.h>
#define ESP_LOGD(tag,fmt,...) do { (void)(tag); if(0) printf(fmt, ##__VA_ARGS__); } while(0)
#define ESP_LOGI ESP_LOGD
#define ESP_LOGW ESP_LOGD
#define ESP_LOGE ESP_LOGD
