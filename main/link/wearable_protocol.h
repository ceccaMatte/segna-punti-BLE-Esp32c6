#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wearable_types.h"

/*
 * Protocol v3 adds 16-bit gesture tokens, simultaneous-button CHORD gestures
 * and chord_window_ms to the configurable timing set.
 *
 * ACTION / ACK semantics remain authoritative: the wearable never computes
 * padel scoring locally.
 */
#define WEARABLE_PROTOCOL_VERSION 3u

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
#define WEARABLE_ACK_PACKET_SIZE 20u
#define WEARABLE_STATUS_PACKET_SIZE 10u

#define WEARABLE_CONFIG_HEADER_SIZE 11u
#define WEARABLE_CONFIG_MAPPING_WIRE_SIZE     (2u + (2u * WEARABLE_MAX_SEQUENCE))
#define WEARABLE_CONFIG_MAX_WIRE_SIZE     (WEARABLE_CONFIG_HEADER_SIZE +      (WEARABLE_MAX_MAPPINGS * WEARABLE_CONFIG_MAPPING_WIRE_SIZE) +      (WEARABLE_ACTION_SOUND_COUNT * 6u))

typedef enum {
    WEARABLE_ACK_OK = 0,
    WEARABLE_ACK_REJECTED = 1,
    WEARABLE_ACK_TEMPORARY_ERROR = 2,
} wearable_ack_status_t;

typedef struct {
    uint16_t revision;
    uint8_t points_a;
    uint8_t points_b;
    uint8_t games_a;
    uint8_t games_b;
    uint8_t sets_a;
    uint8_t sets_b;
} wearable_match_state_t;

typedef struct {
    uint32_t session_id;
    uint32_t sequence;
    wearable_action_t action;
} wearable_action_packet_t;

typedef struct {
    uint32_t session_id;
    uint32_t sequence;
    wearable_ack_status_t status;
    uint8_t transition_flags;
    wearable_match_state_t state;
} wearable_ack_packet_t;

size_t wearable_encode_action(const wearable_action_packet_t *packet,
                              uint8_t *out,
                              size_t capacity);
bool wearable_decode_ack(const uint8_t *in,
                         size_t len,
                         wearable_ack_packet_t *out);

size_t wearable_encode_status(bool commissioned,
                              bool authenticated,
                              bool pairing_open,
                              uint16_t battery_mv,
                              uint8_t *out,
                              size_t capacity);

size_t wearable_encode_config(const wearable_config_t *config,
                              uint8_t *out,
                              size_t capacity);
bool wearable_apply_config_command(wearable_config_t *config,
                                   const uint8_t *in,
                                   size_t len);
