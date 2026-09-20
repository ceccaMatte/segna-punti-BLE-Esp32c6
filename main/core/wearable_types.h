#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WEARABLE_BUTTON_COUNT 4u
#define WEARABLE_BUTTON_MASK_ALL ((uint8_t)((1u << WEARABLE_BUTTON_COUNT) - 1u))
#define WEARABLE_PRIMITIVE_COUNT 5u
#define WEARABLE_MAX_SEQUENCE 8u
#define WEARABLE_MAX_MAPPINGS 16u
#define WEARABLE_ACTION_SOUND_COUNT 4u

typedef uint16_t wearable_token_t;

typedef enum {
    WEARABLE_BUTTON_A = 0,
    WEARABLE_BUTTON_B = 1,
    WEARABLE_BUTTON_MOMENT = 2,
    WEARABLE_BUTTON_UNDO = 3,
} wearable_button_t;

typedef enum {
    WEARABLE_PRIMITIVE_CLICK = 0,
    WEARABLE_PRIMITIVE_DOUBLE = 1,
    WEARABLE_PRIMITIVE_TRIPLE = 2,
    WEARABLE_PRIMITIVE_LONG = 3,
    WEARABLE_PRIMITIVE_CHORD = 4,
} wearable_primitive_t;

typedef enum {
    WEARABLE_ACTION_POINT_A = 0,
    WEARABLE_ACTION_POINT_B = 1,
    WEARABLE_ACTION_MOMENT = 2,
    WEARABLE_ACTION_UNDO = 3,
    WEARABLE_ACTION_ENTER_PAIRING = 4,
} wearable_action_t;

/*
 * Gesture token wire/storage layout (16 bit):
 *   bits 0..3 = button mask (A/B/Moment/Undo)
 *   bits 4..6 = wearable_primitive_t
 *   bits 7..15 reserved
 *
 * Single-button primitives have exactly one bit in the mask.
 * CHORD has at least two bits in the mask.
 */
static inline wearable_token_t wearable_token_from_mask(uint8_t button_mask,
                                                        wearable_primitive_t primitive)
{
    return (wearable_token_t)(((uint16_t)primitive << 4) |
                              (button_mask & WEARABLE_BUTTON_MASK_ALL));
}

static inline wearable_token_t wearable_token(uint8_t button_index,
                                              wearable_primitive_t primitive)
{
    return wearable_token_from_mask((uint8_t)(1u << (button_index & 0x03u)),
                                    primitive);
}

static inline uint8_t wearable_token_button_mask(wearable_token_t token)
{
    return (uint8_t)(token & WEARABLE_BUTTON_MASK_ALL);
}

static inline wearable_primitive_t wearable_token_primitive(wearable_token_t token)
{
    return (wearable_primitive_t)((token >> 4) & 0x07u);
}

static inline uint8_t wearable_popcount4(uint8_t value)
{
    value &= WEARABLE_BUTTON_MASK_ALL;
    return (uint8_t)((value & 1u) +
                     ((value >> 1) & 1u) +
                     ((value >> 2) & 1u) +
                     ((value >> 3) & 1u));
}

static inline bool wearable_token_is_valid(wearable_token_t token)
{
    if ((token & 0xFF80u) != 0u) {
        return false;
    }

    const uint8_t mask = wearable_token_button_mask(token);
    const wearable_primitive_t primitive = wearable_token_primitive(token);

    if (mask == 0u || primitive >= WEARABLE_PRIMITIVE_COUNT) {
        return false;
    }

    const uint8_t buttons = wearable_popcount4(mask);
    return primitive == WEARABLE_PRIMITIVE_CHORD
               ? buttons >= 2u
               : buttons == 1u;
}

typedef struct {
    uint8_t length;
    wearable_token_t tokens[WEARABLE_MAX_SEQUENCE];
} wearable_sequence_t;

typedef struct {
    bool enabled;
    wearable_action_t action;
    wearable_sequence_t sequence;
} wearable_mapping_t;

typedef struct {
    uint16_t frequency_hz;
    uint16_t duty_permille;
    uint16_t duration_ms;
} wearable_tone_t;

typedef struct {
    uint16_t schema_version;
    uint16_t multi_click_gap_ms;
    uint16_t long_press_ms;
    uint16_t sequence_gap_ms;
    uint16_t chord_window_ms;
    uint8_t mapping_count;
    wearable_mapping_t mappings[WEARABLE_MAX_MAPPINGS];
    wearable_tone_t action_sounds[WEARABLE_ACTION_SOUND_COUNT];
} wearable_config_t;

typedef enum {
    WEARABLE_SYS_PAIRING_STARTED = 0,
    WEARABLE_SYS_PAIRING_SUCCESS,
    WEARABLE_SYS_ERROR,
    WEARABLE_SYS_LOW_BATTERY,
    WEARABLE_SYS_GAME_END,
    WEARABLE_SYS_SET_END,
    WEARABLE_SYS_MATCH_END,
} wearable_system_sound_t;

enum {
    WEARABLE_ACK_FLAG_GAME_ENDED  = 1u << 0,
    WEARABLE_ACK_FLAG_SET_ENDED   = 1u << 1,
    WEARABLE_ACK_FLAG_MATCH_ENDED = 1u << 2,
    WEARABLE_ACK_FLAG_ALL = WEARABLE_ACK_FLAG_GAME_ENDED |
                            WEARABLE_ACK_FLAG_SET_ENDED |
                            WEARABLE_ACK_FLAG_MATCH_ENDED,
};
