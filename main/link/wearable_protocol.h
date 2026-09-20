#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wearable_types.h"
#include "config_store.h"

#define WEARABLE_PROTOCOL_VERSION 1u

#define WEARABLE_UUID_SERVICE "c4d10001-6f65-4b6e-ae30-706c61796d6b"
#define WEARABLE_UUID_ACTION  "c4d10002-6f65-4b6e-ae30-706c61796d6b"
#define WEARABLE_UUID_ACK     "c4d10003-6f65-4b6e-ae30-706c61796d6b"
#define WEARABLE_UUID_CONTROL "c4d10004-6f65-4b6e-ae30-706c61796d6b"
#define WEARABLE_UUID_STATUS  "c4d10005-6f65-4b6e-ae30-706c61796d6b"
#define WEARABLE_UUID_CONFIG  "c4d10006-6f65-4b6e-ae30-706c61796d6b"

#define WEARABLE_MSG_ACTION 1u
#define WEARABLE_MSG_ACK 2u
#define WEARABLE_MSG_STATUS 3u
#define WEARABLE_MSG_CONFIG 4u

#define WEARABLE_ACTION_PACKET_SIZE 12u
#define WEARABLE_ACK_PACKET_SIZE 18u
#define WEARABLE_STATUS_PACKET_SIZE 10u

typedef struct {
    uint32_t session_id;
    uint32_t sequence;
    wearable_action_t action;
} wearable_action_packet_t;

typedef struct {
    uint32_t session_id;
    uint32_t sequence;
    uint8_t flags;
    uint8_t points_a;
    uint8_t points_b;
    uint8_t games_a;
    uint8_t games_b;
    uint8_t sets_a;
    uint8_t sets_b;
} wearable_ack_packet_t;

size_t wearable_encode_action(const wearable_action_packet_t *p, uint8_t *out, size_t cap);
bool wearable_decode_ack(const uint8_t *in, size_t len, wearable_ack_packet_t *out);

size_t wearable_encode_status(bool commissioned, bool authenticated, bool pairing_open,
                              uint16_t battery_mv, uint8_t *out, size_t cap);

size_t wearable_encode_config(const wearable_config_t *cfg, uint8_t *out, size_t cap);
bool wearable_apply_config_command(wearable_config_t *cfg, const uint8_t *in, size_t len);
