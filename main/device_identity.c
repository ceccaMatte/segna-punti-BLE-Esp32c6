/**
 * @file device_identity.c
 * @brief Nome breve e nome di annuncio ricavati dall'indirizzo della scheda.
 *
 * SPDX-License-Identifier: MIT
 */

#include "device_identity.h"

#include <stdio.h>

#include "ble_protocol.h"

static const char HEX_DIGITS[] = "0123456789ABCDEF";

void identity_short_id(const uint8_t mac[6], char out[IDENTITY_SHORT_ID_LEN + 1])
{
    if (mac == NULL || out == NULL) {
        return;
    }

    for (int i = 0; i < 2; ++i) {
        const uint8_t byte = mac[IDENTITY_SHORT_ID_OFFSET + i];

        out[i * 2]      = HEX_DIGITS[(byte >> 4) & 0x0Fu];
        out[i * 2 + 1]  = HEX_DIGITS[byte & 0x0Fu];
    }
    out[IDENTITY_SHORT_ID_LEN] = '\0';
}

uint16_t identity_short_id_value(const uint8_t mac[6])
{
    if (mac == NULL) {
        return 0u;
    }

    return (uint16_t)(((uint16_t)mac[IDENTITY_SHORT_ID_OFFSET] << 8) |
                      (uint16_t)mac[IDENTITY_SHORT_ID_OFFSET + 1]);
}

void identity_device_name(const uint8_t mac[6], char *out, size_t out_size)
{
    if (out == NULL || out_size == 0u) {
        return;
    }

    char short_id[IDENTITY_SHORT_ID_LEN + 1];
    identity_short_id(mac, short_id);

    (void)snprintf(out, out_size, "%s%s", PADEL_NAME_PREFIX, short_id);
}
