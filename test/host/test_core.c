#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "config_model.h"
#include "wearable_protocol.h"

static void put_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void test_defaults_are_valid(void)
{
    wearable_config_t cfg;
    wearable_config_set_defaults(&cfg);

    assert(wearable_config_is_valid(&cfg));
    assert(cfg.mapping_count == 5);
    assert(cfg.mappings[0].action == WEARABLE_ACTION_POINT_A);
    assert(cfg.mappings[4].action == WEARABLE_ACTION_ENTER_PAIRING);
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
    assert(out[2] == 0x44 && out[5] == 0x11);
    assert(out[6] == 0x88 && out[9] == 0x55);
    assert(out[10] == WEARABLE_ACTION_MOMENT);

    action.action = WEARABLE_ACTION_ENTER_PAIRING;
    assert(wearable_encode_action(&action, out, sizeof(out)) == 0);
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
    assert(wearable_decode_ack(in, sizeof(in), &ack));
    assert(ack.session_id == 0x12345678u);
    assert(ack.sequence == 42u);
    assert(ack.transition_flags == WEARABLE_ACK_FLAG_GAME_ENDED);
    assert(ack.state.revision == 7u);
    assert(ack.state.points_b == 30u);
    assert(ack.state.games_a == 4u);

    in[3] = 0x80;
    assert(!wearable_decode_ack(in, sizeof(in), &ack));
}

static void test_config_commands_keep_compact_model(void)
{
    wearable_config_t cfg;
    wearable_config_set_defaults(&cfg);

    uint8_t append[12] = {
        0x10,
        5,
        WEARABLE_ACTION_UNDO,
        1,
        wearable_token(WEARABLE_BUTTON_A, WEARABLE_PRIMITIVE_DOUBLE),
    };
    assert(wearable_apply_config_command(&cfg, append, sizeof(append)));
    assert(cfg.mapping_count == 6);
    assert(wearable_config_is_valid(&cfg));

    uint8_t skip[12] = {
        0x10,
        7,
        WEARABLE_ACTION_UNDO,
        1,
        wearable_token(WEARABLE_BUTTON_B, WEARABLE_PRIMITIVE_DOUBLE),
    };
    wearable_config_t before = cfg;
    assert(!wearable_apply_config_command(&cfg, skip, sizeof(skip)));
    assert(memcmp(&before, &cfg, sizeof(cfg)) == 0);
}

static void test_pairing_mapping_cannot_be_removed(void)
{
    wearable_config_t cfg;
    wearable_config_set_defaults(&cfg);

    const uint8_t remove_pairing[] = {0x11, 4};
    wearable_config_t before = cfg;

    assert(!wearable_apply_config_command(
        &cfg,
        remove_pairing,
        sizeof(remove_pairing)));
    assert(memcmp(&before, &cfg, sizeof(cfg)) == 0);
}

static void test_duplicate_gesture_rejected(void)
{
    wearable_config_t cfg;
    wearable_config_set_defaults(&cfg);

    uint8_t duplicate[12] = {
        0x10,
        1,
        WEARABLE_ACTION_POINT_B,
        1,
        wearable_token(WEARABLE_BUTTON_A, WEARABLE_PRIMITIVE_CLICK),
    };

    wearable_config_t before = cfg;
    assert(!wearable_apply_config_command(&cfg, duplicate, sizeof(duplicate)));
    assert(memcmp(&before, &cfg, sizeof(cfg)) == 0);
}

int main(void)
{
    test_defaults_are_valid();
    test_action_packet();
    test_ack_packet();
    test_config_commands_keep_compact_model();
    test_pairing_mapping_cannot_be_removed();
    test_duplicate_gesture_rejected();

    puts("core tests: OK");
    return 0;
}
