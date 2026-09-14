/**
 * @file test_device_identity.c
 * @brief Test del nome breve ricavato dall'indirizzo della scheda.
 *
 * SPDX-License-Identifier: MIT
 */

#include "device_identity.h"

#include <string.h>

#include "ble_protocol.h"
#include "test_util.h"

/* Un indirizzo come quelli veri delle schede di questo tipo. */
static const uint8_t MAC[6] = { 0x24, 0x0Au, 0xC4u, 0x00u, 0xA3u, 0x1Fu };

static void test_nome_breve(void)
{
    const char *name = "identita': il nome breve sono le ultime due cifre";
    test_begin(name);

    char short_id[IDENTITY_SHORT_ID_LEN + 1];
    identity_short_id(MAC, short_id);

    CHECK_EQ(short_id[0], 'A');
    CHECK_EQ(short_id[1], '3');
    CHECK_EQ(short_id[2], '1');
    CHECK_EQ(short_id[3], 'F');
    CHECK_EQ(short_id[4], '\0');

    CHECK_EQ(identity_short_id_value(MAC), 0xA31Fu);

    /* Uno zero davanti non deve sparire: il nome resta di quattro cifre. */
    const uint8_t zeros[6] = { 1u, 2u, 3u, 4u, 0u, 5u };
    identity_short_id(zeros, short_id);
    CHECK_EQ(short_id[0], '0');
    CHECK_EQ(short_id[1], '0');
    CHECK_EQ(short_id[2], '0');
    CHECK_EQ(short_id[3], '5');
    CHECK_EQ(identity_short_id_value(zeros), 5u);

    test_end(name);
}

static void test_nome_annuncio(void)
{
    const char *name = "identita': il nome annunciato";
    test_begin(name);

    char buffer[IDENTITY_NAME_MAX_LEN];
    identity_device_name(MAC, buffer, sizeof(buffer));

    static const char EXPECTED[] = "PADEL_SCORE_A31F";

    CHECK_EQ(strlen(buffer), sizeof(EXPECTED) - 1u);
    for (size_t i = 0; i < sizeof(EXPECTED); ++i) {
        CHECK_EQ(buffer[i], EXPECTED[i]);
    }

    /* Il nome deve stare nel pacchetto di risposta allo scan, che ne porta
       trentuno di byte in tutto: il nome piu' la sua intestazione. */
    CHECK(strlen(buffer) + 2u <= 31u);

    /* Il prefisso e' quello che la pagina web usa per filtrare. */
    for (size_t i = 0; i < sizeof(PADEL_NAME_PREFIX) - 1u; ++i) {
        CHECK_EQ(buffer[i], PADEL_NAME_PREFIX[i]);
    }

    test_end(name);
}

static void test_buffer_piccolo(void)
{
    const char *name = "identita': un buffer piccolo non viene calpestato";
    test_begin(name);

    char small[8];
    for (size_t i = 0; i < sizeof(small); ++i) {
        small[i] = (char)0x7F;
    }

    identity_device_name(MAC, small, sizeof(small));

    /* Il nome viene tagliato ma la stringa resta chiusa, e nessuno degli altri
       byte viene toccato: e' il caso che nella pratica non capita mai, ma che
       se capita non deve fare danni. */
    CHECK_EQ(small[sizeof(small) - 1], '\0');
    for (size_t i = 0; i < sizeof(small) - 1u; ++i) {
        CHECK(small[i] != (char)0x7F);
    }

    /* Chiamate senza senso: non devono far cadere niente. */
    identity_device_name(MAC, NULL, 16u);
    identity_device_name(MAC, small, 0u);
    identity_short_id(NULL, small);
    CHECK_EQ(identity_short_id_value(NULL), 0u);

    test_end(name);
}

/* -------------------------------------------------------------------------- */

void test_device_identity_all(void)
{
    printf("Identita' della scheda\n");

    test_nome_breve();
    test_nome_annuncio();
    test_buffer_piccolo();

    printf("\n");
}
