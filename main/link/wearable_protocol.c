#include "wearable_protocol.h"

#include <string.h>

#include "config_model.h"

#define CONFIG_CMD_SET_MAPPING 0x10u
#define CONFIG_CMD_DELETE_MAPPING 0x11u
#define CONFIG_CMD_SET_ACTION_SOUND 0x12u
#define CONFIG_CMD_SET_TIMINGS 0x13u
#define CONFIG_CMD_RESET_DEFAULTS 0x14u

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

size_t wearable_encode_action(const wearable_action_packet_t *p, uint8_t *out, size_t cap)
{
    if (p == NULL || out == NULL || cap < WEARABLE_ACTION_PACKET_SIZE ||
        p->action > WEARABLE_ACTION_UNDO) {
        return 0;
    }

    out[0] = WEARABLE_PROTOCOL_VERSION;
    out[1] = WEARABLE_MSG_ACTION;
    put32(&out[2], p->session_id);
    put32(&out[6], p->sequence);
    out[10] = (uint8_t)p->action;
    out[11] = 0;
    return WEARABLE_ACTION_PACKET_SIZE;
}

bool wearable_decode_ack(const uint8_t *in, size_t len, wearable_ack_packet_t *out)
{
    if (in == NULL || out == NULL ||
        len != WEARABLE_ACK_PACKET_SIZE ||
        in[0] != WEARABLE_PROTOCOL_VERSION ||
        in[1] != WEARABLE_MSG_ACK ||
        in[2] > WEARABLE_ACK_TEMPORARY_ERROR ||
        (in[3] & (uint8_t)~WEARABLE_ACK_FLAG_ALL) != 0u) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->status = (wearable_ack_status_t)in[2];
    out->transition_flags = in[3];
    out->session_id = get32(&in[4]);
    out->sequence = get32(&in[8]);
    out->state.revision = get16(&in[12]);
    out->state.points_a = in[14];
    out->state.points_b = in[15];
    out->state.games_a = in[16];
    out->state.games_b = in[17];
    out->state.sets_a = in[18];
    out->state.sets_b = in[19];
    return true;
}

size_t wearable_encode_status(bool commissioned, bool authenticated, bool pairing_open,
                              uint16_t battery_mv, uint8_t *out, size_t cap)
{
    if (out == NULL || cap < WEARABLE_STATUS_PACKET_SIZE) {
        return 0;
    }

    memset(out, 0, WEARABLE_STATUS_PACKET_SIZE);
    out[0] = WEARABLE_PROTOCOL_VERSION;
    out[1] = WEARABLE_MSG_STATUS;
    out[2] = commissioned ? 1u : 0u;
    out[3] = authenticated ? 1u : 0u;
    out[4] = pairing_open ? 1u : 0u;
    put16(&out[5], battery_mv);
    return WEARABLE_STATUS_PACKET_SIZE;
}

size_t wearable_encode_config(const wearable_config_t *cfg, uint8_t *out, size_t cap)
{
    if (!wearable_config_is_valid(cfg) || out == NULL) {
        return 0;
    }

    size_t wanted = 9u +
                    (size_t)cfg->mapping_count * (2u + WEARABLE_MAX_SEQUENCE) +
                    WEARABLE_ACTION_SOUND_COUNT * 6u;
    if (cap < wanted) {
        return 0;
    }

    out[0] = WEARABLE_PROTOCOL_VERSION;
    out[1] = WEARABLE_MSG_CONFIG;
    out[2] = cfg->mapping_count;
    put16(&out[3], cfg->multi_click_gap_ms);
    put16(&out[5], cfg->long_press_ms);
    put16(&out[7], cfg->sequence_gap_ms);

    size_t o = 9;
    for (uint8_t i = 0; i < cfg->mapping_count; ++i) {
        const wearable_mapping_t *m = &cfg->mappings[i];
        out[o++] = (uint8_t)m->action;
        out[o++] = m->sequence.length;
        memset(&out[o], 0, WEARABLE_MAX_SEQUENCE);
        memcpy(&out[o], m->sequence.tokens, m->sequence.length);
        o += WEARABLE_MAX_SEQUENCE;
    }

    for (uint8_t i = 0; i < WEARABLE_ACTION_SOUND_COUNT; ++i) {
        put16(&out[o], cfg->action_sounds[i].frequency_hz); o += 2;
        put16(&out[o], cfg->action_sounds[i].duty_permille); o += 2;
        put16(&out[o], cfg->action_sounds[i].duration_ms); o += 2;
    }

    return o;
}

bool wearable_apply_config_command(wearable_config_t *cfg, const uint8_t *in, size_t len)
{
    if (cfg == NULL || in == NULL || len == 0) {
        return false;
    }

    wearable_config_t next = *cfg;

    switch (in[0]) {
    case CONFIG_CMD_SET_MAPPING: {
        if (len != 12u) {
            return false;
        }

        const uint8_t index = in[1];
        const uint8_t action = in[2];
        const uint8_t n = in[3];

        /*
         * An existing mapping may be replaced, or exactly one new mapping may
         * be appended. Skipping indices would create disabled holes that the
         * BLE representation cannot express.
         */
        if (index > next.mapping_count ||
            index >= WEARABLE_MAX_MAPPINGS ||
            action > WEARABLE_ACTION_ENTER_PAIRING ||
            n == 0 || n > WEARABLE_MAX_SEQUENCE) {
            return false;
        }

        if (index == next.mapping_count) {
            next.mapping_count++;
        }

        wearable_mapping_t *m = &next.mappings[index];
        memset(m, 0, sizeof(*m));
        m->enabled = true;
        m->action = (wearable_action_t)action;
        m->sequence.length = n;
        memcpy(m->sequence.tokens, &in[4], n);
        break;
    }

    case CONFIG_CMD_DELETE_MAPPING: {
        if (len != 2u || in[1] >= next.mapping_count) {
            return false;
        }

        const uint8_t idx = in[1];
        for (uint8_t i = idx; i + 1u < next.mapping_count; ++i) {
            next.mappings[i] = next.mappings[i + 1u];
        }
        next.mapping_count--;
        memset(&next.mappings[next.mapping_count], 0, sizeof(next.mappings[0]));
        break;
    }

    case CONFIG_CMD_SET_ACTION_SOUND: {
        if (len != 8u || in[1] >= WEARABLE_ACTION_SOUND_COUNT) {
            return false;
        }

        wearable_tone_t *tone = &next.action_sounds[in[1]];
        tone->frequency_hz = get16(&in[2]);
        tone->duty_permille = get16(&in[4]);
        tone->duration_ms = get16(&in[6]);
        break;
    }

    case CONFIG_CMD_SET_TIMINGS:
        if (len != 7u) {
            return false;
        }
        next.sequence_gap_ms = get16(&in[1]);
        next.multi_click_gap_ms = get16(&in[3]);
        next.long_press_ms = get16(&in[5]);
        break;

    case CONFIG_CMD_RESET_DEFAULTS:
        if (len != 1u) {
            return false;
        }
        wearable_config_set_defaults(&next);
        break;

    default:
        return false;
    }

    if (!wearable_config_is_valid(&next)) {
        return false;
    }

    *cfg = next;
    return true;
}
