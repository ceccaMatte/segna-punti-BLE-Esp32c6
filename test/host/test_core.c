#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "config_model.h"
#include "wearable_protocol.h"

static void put_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void test_tokens(void)
{
    const wearable_token_t click_a =
        wearable_token(WEARABLE_BUTTON_A,
                       WEARABLE_PRIMITIVE_CLICK);
    assert(wearable_token_is_valid(click_a));
    assert(wearable_token_button_mask(click_a) ==
           (1u << WEARABLE_BUTTON_A));

    const wearable_token_t chord_ab =
        wearable_token_from_mask(
            (uint8_t)((1u << WEARABLE_BUTTON_A) |
                      (1u << WEARABLE_BUTTON_B)),
            WEARABLE_PRIMITIVE_CHORD);
    assert(wearable_token_is_valid(chord_ab));
    assert(wearable_popcount4(
               wearable_token_button_mask(chord_ab)) == 2u);

    assert(!wearable_token_is_valid(
        wearable_token_from_mask(
            (uint8_t)(1u << WEARABLE_BUTTON_A),
            WEARABLE_PRIMITIVE_CHORD)));

    assert(!wearable_token_is_valid(
        wearable_token_from_mask(
            (uint8_t)((1u << WEARABLE_BUTTON_A) |
                      (1u << WEARABLE_BUTTON_B)),
            WEARABLE_PRIMITIVE_CLICK)));
}

static void test_defaults_are_valid(void)
{
    wearable_config_t config;
    wearable_config_set_defaults(&config);

    assert(wearable_config_is_valid(&config));
    assert(config.schema_version ==
           WEARABLE_CONFIG_SCHEMA_VERSION);
    assert(config.mapping_count == 6u);
    assert(config.chord_window_ms == 60u);
    assert(config.mappings[0].action ==
           WEARABLE_ACTION_POINT_A);
    assert(config.mappings[4].action ==
           WEARABLE_ACTION_UNDO);
    assert(wearable_token_primitive(
               config.mappings[4].sequence.tokens[0]) ==
           WEARABLE_PRIMITIVE_CHORD);
    assert(config.mappings[5].action ==
           WEARABLE_ACTION_ENTER_PAIRING);
}

static void test_action_packet(void)
{
    wearable_action_packet_t action = {
        .session_id = 0x11223344u,
        .sequence = 0x55667788u,
        .action = WEARABLE_ACTION_MOMENT,
    };
    uint8_t out[WEARABLE_ACTION_PACKET_SIZE] = {0};

    assert(wearable_encode_action(
               &action,
               out,
               sizeof(out)) ==
           WEARABLE_ACTION_PACKET_SIZE);
    assert(out[0] == WEARABLE_PROTOCOL_VERSION);
    assert(out[1] == WEARABLE_MSG_ACTION);
    assert(out[2] == 0x44 && out[5] == 0x11);
    assert(out[6] == 0x88 && out[9] == 0x55);
    assert(out[10] == WEARABLE_ACTION_MOMENT);

    action.action = WEARABLE_ACTION_ENTER_PAIRING;
    assert(wearable_encode_action(
               &action,
               out,
               sizeof(out)) == 0u);
}

static void test_ack_packet(void)
{
    uint8_t in[WEARABLE_ACK_PACKET_SIZE] = {0};
    in[0] = WEARABLE_PROTOCOL_VERSION;
    in[1] = WEARABLE_MSG_ACK;
    in[2] = WEARABLE_ACK_OK;
    in[3] = WEARABLE_ACK_FLAG_GAME_ENDED;
    put_u32(&in[4], 0x12345678u);
    put_u32(&in[8], 42u);
    in[12] = 7;
    in[13] = 0;
    in[14] = 0;
    in[15] = 30;
    in[16] = 4;
    in[17] = 3;
    in[18] = 1;
    in[19] = 0;

    wearable_ack_packet_t ack;
    assert(wearable_decode_ack(
        in,
        sizeof(in),
        &ack));
    assert(ack.session_id == 0x12345678u);
    assert(ack.sequence == 42u);
    assert(ack.transition_flags ==
           WEARABLE_ACK_FLAG_GAME_ENDED);
    assert(ack.state.revision == 7u);
    assert(ack.state.points_b == 30u);
    assert(ack.state.games_a == 4u);

    in[3] = 0x80;
    assert(!wearable_decode_ack(
        in,
        sizeof(in),
        &ack));
}

static void test_config_wire_contains_chord(void)
{
    wearable_config_t config;
    wearable_config_set_defaults(&config);

    uint8_t out[WEARABLE_CONFIG_MAX_WIRE_SIZE] = {0};
    const size_t size =
        wearable_encode_config(
            &config,
            out,
            sizeof(out));

    assert(size ==
           WEARABLE_CONFIG_HEADER_SIZE +
               config.mapping_count *
                   WEARABLE_CONFIG_MAPPING_WIRE_SIZE +
               WEARABLE_ACTION_SOUND_COUNT * 6u);

    assert(out[0] == WEARABLE_PROTOCOL_VERSION);
    assert(out[1] == WEARABLE_MSG_CONFIG);
    assert(out[2] == config.mapping_count);
    assert(out[9] == 60u && out[10] == 0u);

    const size_t chord_offset =
        WEARABLE_CONFIG_HEADER_SIZE +
        4u * WEARABLE_CONFIG_MAPPING_WIRE_SIZE;
    assert(out[chord_offset] == WEARABLE_ACTION_UNDO);
    assert(out[chord_offset + 1] == 1u);

    const wearable_token_t chord_wire =
        (wearable_token_t)out[chord_offset + 2] |
        ((wearable_token_t)out[chord_offset + 3] << 8);
    assert(chord_wire ==
           config.mappings[4].sequence.tokens[0]);
}

static void test_mapping_commands_keep_compact_model(void)
{
    wearable_config_t config;
    wearable_config_set_defaults(&config);

    uint8_t append[4 + 2 * WEARABLE_MAX_SEQUENCE] = {0};
    append[0] = 0x10;
    append[1] = 6;
    append[2] = WEARABLE_ACTION_UNDO;
    append[3] = 1;

    const wearable_token_t chord =
        wearable_token_from_mask(
            (uint8_t)((1u << WEARABLE_BUTTON_A) |
                      (1u << WEARABLE_BUTTON_MOMENT)),
            WEARABLE_PRIMITIVE_CHORD);
    put_u16(&append[4], chord);

    assert(wearable_apply_config_command(
        &config,
        append,
        sizeof(append)));
    assert(config.mapping_count == 7u);
    assert(wearable_config_is_valid(&config));

    uint8_t skip[4 + 2 * WEARABLE_MAX_SEQUENCE] = {0};
    skip[0] = 0x10;
    skip[1] = 8;
    skip[2] = WEARABLE_ACTION_UNDO;
    skip[3] = 1;
    put_u16(&skip[4],
            wearable_token(
                WEARABLE_BUTTON_B,
                WEARABLE_PRIMITIVE_DOUBLE));

    wearable_config_t before = config;
    assert(!wearable_apply_config_command(
        &config,
        skip,
        sizeof(skip)));
    assert(memcmp(&before,
                  &config,
                  sizeof(config)) == 0);
}

static void test_timing_command_updates_chord_window(void)
{
    wearable_config_t config;
    wearable_config_set_defaults(&config);

    uint8_t command[9] = {0};
    command[0] = 0x13;
    put_u16(&command[1], 350);
    put_u16(&command[3], 280);
    put_u16(&command[5], 1000);
    put_u16(&command[7], 75);

    assert(wearable_apply_config_command(
        &config,
        command,
        sizeof(command)));
    assert(config.sequence_gap_ms == 350u);
    assert(config.multi_click_gap_ms == 280u);
    assert(config.long_press_ms == 1000u);
    assert(config.chord_window_ms == 75u);
}

static void test_pairing_mapping_cannot_be_removed(void)
{
    wearable_config_t config;
    wearable_config_set_defaults(&config);

    const uint8_t remove_pairing[] = {0x11, 5};
    wearable_config_t before = config;

    assert(!wearable_apply_config_command(
        &config,
        remove_pairing,
        sizeof(remove_pairing)));
    assert(memcmp(&before,
                  &config,
                  sizeof(config)) == 0);
}

static void test_duplicate_gesture_rejected(void)
{
    wearable_config_t config;
    wearable_config_set_defaults(&config);

    uint8_t duplicate[4 + 2 * WEARABLE_MAX_SEQUENCE] = {0};
    duplicate[0] = 0x10;
    duplicate[1] = 1;
    duplicate[2] = WEARABLE_ACTION_POINT_B;
    duplicate[3] = 1;
    put_u16(&duplicate[4],
            wearable_token(
                WEARABLE_BUTTON_A,
                WEARABLE_PRIMITIVE_CLICK));

    wearable_config_t before = config;

    assert(!wearable_apply_config_command(
        &config,
        duplicate,
        sizeof(duplicate)));
    assert(memcmp(&before,
                  &config,
                  sizeof(config)) == 0);
}

int main(void)
{
    test_tokens();
    test_defaults_are_valid();
    test_action_packet();
    test_ack_packet();
    test_config_wire_contains_chord();
    test_mapping_commands_keep_compact_model();
    test_timing_command_updates_chord_window();
    test_pairing_mapping_cannot_be_removed();
    test_duplicate_gesture_rejected();

    puts("core tests: OK");
    return 0;
}
