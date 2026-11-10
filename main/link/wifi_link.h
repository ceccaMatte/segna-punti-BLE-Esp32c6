#pragma once

#include "esp_http_server.h"
#include "wearable_protocol.h"

typedef void (*wifi_ack_cb_t)(wearable_action_t action,
                              const wearable_ack_packet_t *ack, void *ctx);
typedef void (*wifi_action_failed_cb_t)(wearable_action_t action, uint32_t sequence, void *ctx);
esp_err_t wifi_link_start(wifi_ack_cb_t cb, wifi_action_failed_cb_t failed_cb, void *ctx);
esp_err_t wifi_link_register(httpd_handle_t server);
bool wifi_link_send_action(wearable_action_t action);
bool wifi_link_is_connected(void);
