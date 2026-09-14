/**
 * @file ble_protocol.c
 * @brief Codifica e decodifica dei pacchetti scambiati con la pagina web.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ble_protocol.h"

#include <string.h>

/* -------------------------------------------------------------------------- */
/* Byte, nell'ordine giusto                                                   */
/* -------------------------------------------------------------------------- */

/*
 * I numeri piu' lunghi di un byte viaggiano con il byte meno significativo per
 * primo, come fa l'ESP32 e come si aspetta chi legge in JavaScript con
 * DataView.getUint16(offset, true). Scriverlo una volta sola qui evita di
 * doverlo ricordare in ognuna delle codifiche.
 */

static void put_u16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static uint16_t get_u16(const uint8_t *in)
{
    return (uint16_t)((uint16_t)in[0] | ((uint16_t)in[1] << 8));
}

/* -------------------------------------------------------------------------- */
/* Intestazione comune                                                        */
/* -------------------------------------------------------------------------- */

/** Scrive i due byte di testa: versione e tipo di messaggio. */
static void put_header(uint8_t type, uint8_t *out)
{
    out[0] = PADEL_PROTOCOL_VERSION;
    out[1] = type;
}

/**
 * Controlla i due byte di testa e la lunghezza.
 *
 * Una lunghezza maggiore di quella attesa si accetta: il pacchetto potrebbe
 * arrivare da una versione futura che ha aggiunto campi in coda, e i campi che
 * conosciamo restano comunque al loro posto.
 */
static bool header_ok(const uint8_t *in, size_t in_size, size_t wanted, uint8_t type)
{
    if (in == NULL || in_size < wanted) {
        return false;
    }
    if (in[0] != PADEL_PROTOCOL_VERSION) {
        return false;
    }
    return in[1] == type;
}

/* -------------------------------------------------------------------------- */
/* Stato della partita                                                        */
/* -------------------------------------------------------------------------- */

size_t padel_score_encode(const padel_score_packet_t *packet, uint8_t *out, size_t out_size)
{
    if (packet == NULL || out == NULL || out_size < PADEL_SCORE_PACKET_SIZE) {
        return 0u;
    }

    put_header(PADEL_MSG_SCORE_STATE, out);
    out[2] = packet->flags;
    out[3] = packet->winner;
    put_u16(&out[4], packet->sequence);

    for (uint8_t side = 0; side < 2u; ++side) {
        out[6u + side] = packet->points[side];
        out[8u + side] = packet->games[side];
        out[10u + side] = packet->sets[side];
    }

    put_u16(&out[12], packet->tb_points[0]);
    put_u16(&out[14], packet->tb_points[1]);

    return PADEL_SCORE_PACKET_SIZE;
}

bool padel_score_decode(const uint8_t *in, size_t in_size, padel_score_packet_t *out)
{
    if (out == NULL || !header_ok(in, in_size, PADEL_SCORE_PACKET_SIZE, PADEL_MSG_SCORE_STATE)) {
        return false;
    }

    memset(out, 0, sizeof(*out));

    out->flags    = in[2];
    out->winner   = in[3];
    out->sequence = get_u16(&in[4]);

    for (uint8_t side = 0; side < 2u; ++side) {
        out->points[side] = in[6u + side];
        out->games[side]  = in[8u + side];
        out->sets[side]   = in[10u + side];
    }

    out->tb_points[0] = get_u16(&in[12]);
    out->tb_points[1] = get_u16(&in[14]);

    return true;
}

/* -------------------------------------------------------------------------- */
/* Stato dell'associazione                                                    */
/* -------------------------------------------------------------------------- */

size_t padel_status_encode(const padel_status_packet_t *packet, uint8_t *out, size_t out_size)
{
    if (packet == NULL || out == NULL || out_size < PADEL_STATUS_PACKET_SIZE) {
        return 0u;
    }

    put_header(PADEL_MSG_COMMISSIONING_STATUS, out);
    out[2] = packet->state;
    out[3] = packet->authenticated;
    out[4] = packet->result;
    out[5] = packet->remaining_s;

    return PADEL_STATUS_PACKET_SIZE;
}

bool padel_status_decode(const uint8_t *in, size_t in_size, padel_status_packet_t *out)
{
    if (out == NULL || !header_ok(in, in_size, PADEL_STATUS_PACKET_SIZE, PADEL_MSG_COMMISSIONING_STATUS)) {
        return false;
    }

    out->state         = in[2];
    out->authenticated = in[3];
    out->result        = in[4];
    out->remaining_s   = in[5];

    return true;
}

/* -------------------------------------------------------------------------- */
/* Chi e' la scheda                                                           */
/* -------------------------------------------------------------------------- */

size_t padel_device_info_encode(const padel_device_info_packet_t *packet, uint8_t *out, size_t out_size)
{
    if (packet == NULL || out == NULL || out_size < PADEL_DEVICE_INFO_SIZE) {
        return 0u;
    }

    put_header(PADEL_MSG_DEVICE_INFO, out);
    out[2] = packet->state;
    out[3] = packet->authenticated;
    put_u16(&out[4], packet->firmware);
    put_u16(&out[6], packet->short_id);

    return PADEL_DEVICE_INFO_SIZE;
}

bool padel_device_info_decode(const uint8_t *in, size_t in_size, padel_device_info_packet_t *out)
{
    if (out == NULL || !header_ok(in, in_size, PADEL_DEVICE_INFO_SIZE, PADEL_MSG_DEVICE_INFO)) {
        return false;
    }

    out->state         = in[2];
    out->authenticated = in[3];
    out->firmware      = get_u16(&in[4]);
    out->short_id      = get_u16(&in[6]);

    return true;
}

/* -------------------------------------------------------------------------- */
/* Comandi                                                                    */
/* -------------------------------------------------------------------------- */

size_t padel_control_encode(const padel_control_packet_t *packet, uint8_t *out, size_t out_size)
{
    if (packet == NULL || out == NULL || out_size < PADEL_CONTROL_PACKET_SIZE) {
        return 0u;
    }

    out[0] = packet->opcode;
    memcpy(&out[1], packet->token, PADEL_TOKEN_LEN);

    return PADEL_CONTROL_PACKET_SIZE;
}

bool padel_control_decode(const uint8_t *in, size_t in_size, padel_control_packet_t *out)
{
    if (in == NULL || out == NULL) {
        return false;
    }

    /* I comandi hanno una lunghezza fissa: qui non c'e' nessun campo che possa
       crescere in futuro, quindi un pacchetto piu' corto o piu' lungo e' un
       errore e non un pacchetto di una versione nuova. */
    if (in_size != PADEL_CONTROL_PACKET_SIZE) {
        return false;
    }

    if (in[0] != (uint8_t)PADEL_OP_CLAIM && in[0] != (uint8_t)PADEL_OP_AUTH) {
        return false;
    }

    out->opcode = in[0];
    memcpy(out->token, &in[1], PADEL_TOKEN_LEN);

    return true;
}
