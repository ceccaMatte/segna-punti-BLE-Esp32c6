#pragma once
#include <stddef.h>
#include "wearable_protocol.h"
#include "cJSON.h"
#define RX_USB_LINE_MAX 1024
typedef enum {RX_HELLO, RX_READY, RX_PING, RX_GET_STATUS, RX_ACK} rx_message_type_t;
typedef struct {
    rx_message_type_t type;
    uint32_t nonce;
    char wearable_id[13];
    wearable_ack_packet_t ack;
} rx_message_t;
bool rx_id_valid(const char *id);
bool rx_json_uint(const cJSON *object, const char *key, uint32_t max, uint32_t *out);
bool rx_message_parse(const char *line, rx_message_t *out, const char **error);
const char *rx_command(wearable_action_t action);
cJSON *rx_score_json(const wearable_match_state_t *score);
