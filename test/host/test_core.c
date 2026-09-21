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
        wearable_token(WEARABLE_BUTTON_A, WEARABLE_PRIMITIVE_CLICK);
    assert(wearable_token_is_valid(click_a));

    const uint8_t ab =
        (uint8_t)((1u << WEARABLE_BUTTON_A) |
                  (1u << WEARABLE_BUTTON_B));

    assert(wearable_token_is_valid(
        wearable_token_from_mask(ab, WEARABLE_PRIMITIVE_CLICK)));
    assert(wearable_token_is_valid(
        wearable_token_from_mask(ab, WEARABLE_PRIMITIVE_DOUBLE)));
    assert(wearable_token_is_valid(
        wearable_token_from_mask(ab, WEARABLE_PRIMITIVE_TRIPLE)));
    assert(wearable_token_is_valid(
        wearable_token_from_mask(ab, WEARABLE_PRIMITIVE_LONG)));

    assert(!wearable_token_is_valid(
        wearable_token_from_mask(0, WEARABLE_PRIMITIVE_CLICK)));
}

static void test_defaults_are_valid(void)
{
    wearable_config_t config;
    wearable_config_set_defaults(&config);

    assert(wearable_config_is_valid(&config));
    assert(config.schema_version == WEARABLE_CONFIG_SCHEMA_VERSION);
    assert(config.mapping_count == 8u);
    assert(config.multi_click_gap_ms == 280u);
    assert(config.long_press_ms == 450u);
    assert(config.sequence_gap_ms == 300u);
    assert(config.simultaneous_window_ms == 60u);

    const uint8_t ab =
        (uint8_t)((1u << WEARABLE_BUTTON_A) |
                  (1u << WEARABLE_BUTTON_B));
    assert(config.mappings[4].action == WEARABLE_ACTION_UNDO);
    assert(config.mappings[4].sequence.tokens[0] ==
           wearable_token_from_mask(ab, WEARABLE_PRIMITIVE_CLICK));

    assert(config.mappings[5].action == WEARABLE_ACTION_POINT_A);
    assert(config.mappings[5].sequence.tokens[0] ==
           wearable_token_from_mask(ab, WEARABLE_PRIMITIVE_DOUBLE));

    assert(config.mappings[6].action == WEARABLE_ACTION_VAR);
    assert(config.mappings[6].sequence.tokens[0] ==
           wearable_token(WEARABLE_BUTTON_MOMENT,
                          WEARABLE_PRIMITIVE_DOUBLE));

    assert(config.mappings[7].action == WEARABLE_ACTION_ENTER_PAIRING);
    assert(config.mappings[7].sequence.tokens[0] ==
           wearable_token(WEARABLE_BUTTON_UNDO,
                          WEARABLE_PRIMITIVE_LONG));
}

static void test_action_packet(void)
{
    wearable_action_packet_t action = {
        .session_id = 0x11223344u,
        .sequence = 0x55667788u,
        .action = WEARABLE_ACTION_MOMENT,
    };
    uint8_t out[WEARABLE_ACTION_PACKET_SIZE] = {0};

    assert(wearable_encode_action(&action, out, sizeof(out)) ==
           WEARABLE_ACTION_PACKET_SIZE);
    assert(out[0] == WEARABLE_PROTOCOL_VERSION);
    assert(out[1] == WEARABLE_MSG_ACTION);

    action.action = WEARABLE_ACTION_VAR;
    assert(wearable_encode_action(&action, out, sizeof(out)) ==
           WEARABLE_ACTION_PACKET_SIZE);
    assert(out[10] == WEARABLE_ACTION_VAR);

    action.action = WEARABLE_ACTION_ENTER_PAIRING;
    assert(wearable_encode_action(&action, out, sizeof(out)) == 0u);
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
    put_u16(&in[12], 7u);
    in[15] = 30;
    in[16] = 4;

    wearable_ack_packet_t ack;
    assert(wearable_decode_ack(in, sizeof(in), &ack));
    assert(ack.sequence == 42u);
    assert(ack.state.revision == 7u);

    in[3] = 0x80;
    assert(!wearable_decode_ack(in, sizeof(in), &ack));
}

static void test_config_wire(void)
{
    wearable_config_t config;
    wearable_config_set_defaults(&config);

    uint8_t out[WEARABLE_CONFIG_MAX_WIRE_SIZE] = {0};
    const size_t size =
        wearable_encode_config(&config, out, sizeof(out));

    assert(size ==
           WEARABLE_CONFIG_HEADER_SIZE +
           config.mapping_count * WEARABLE_CONFIG_MAPPING_WIRE_SIZE +
           WEARABLE_ACTION_SOUND_COUNT * 6u);
    assert(out[0] == WEARABLE_PROTOCOL_VERSION);
    assert(out[2] == config.mapping_count);
    assert(out[9] == 60u && out[10] == 0u);
}

static void test_mapping_commands(void)
{
    wearable_config_t config;
    wearable_config_set_defaults(&config);

    uint8_t append[4 + 2 * WEARABLE_MAX_SEQUENCE] = {0};
    append[0] = 0x10;
    append[1] = 8;
    append[2] = WEARABLE_ACTION_UNDO;
    append[3] = 1;

    const wearable_token_t token =
        wearable_token_from_mask(
            (uint8_t)((1u << WEARABLE_BUTTON_A) |
                      (1u << WEARABLE_BUTTON_MOMENT)),
            WEARABLE_PRIMITIVE_TRIPLE);
    put_u16(&append[4], token);

    assert(wearable_apply_config_command(&config, append, sizeof(append)));
    assert(config.mapping_count == 9u);
    assert(wearable_config_is_valid(&config));
}

static void test_timings(void)
{
    wearable_config_t config;
    wearable_config_set_defaults(&config);

    uint8_t command[9] = {0};
    command[0] = 0x13;
    put_u16(&command[1], 350);
    put_u16(&command[3], 280);
    put_u16(&command[5], 1000);
    put_u16(&command[7], 75);

    assert(wearable_apply_config_command(&config, command, sizeof(command)));
    assert(config.sequence_gap_ms == 350u);
    assert(config.multi_click_gap_ms == 280u);
    assert(config.long_press_ms == 1000u);
    assert(config.simultaneous_window_ms == 75u);
}

static void test_pairing_cannot_be_removed(void)
{
    wearable_config_t config;
    wearable_config_set_defaults(&config);

    const uint8_t remove_pairing[] = {0x11, 7};
    wearable_config_t before = config;
    assert(!wearable_apply_config_command(&config,
                                          remove_pairing,
                                          sizeof(remove_pairing)));
    assert(memcmp(&before, &config, sizeof(config)) == 0);
}

int main(void)
{
    test_tokens();
    test_defaults_are_valid();
    test_action_packet();
    test_ack_packet();
    test_config_wire();
    test_mapping_commands();
    test_timings();
    test_pairing_cannot_be_removed();
    puts("core tests: OK");
    return 0;
}
