/**
 * @file test_ble_protocol.c
 * @brief Test della disposizione dei byte dei pacchetti BLE.
 *
 * La disposizione dei byte e' un contratto fra due programmi scritti in due
 * linguaggi diversi. Il caso piu' importante di questo file e' quello che
 * confronta i byte uno per uno: se qualcuno cambia l'ordine dei campi, qui si
 * vede subito invece di scoprirlo con una pagina web che mostra numeri strani.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ble_protocol.h"

#include "test_util.h"

/* -------------------------------------------------------------------------- */

static void test_dimensioni(void)
{
    const char *name = "protocollo: le dimensioni dei pacchetti sono quelle";
    test_begin(name);

    /* Numeri scritti a mano di proposito: sono parte del contratto. */
    CHECK_EQ(PADEL_SCORE_PACKET_SIZE, 16);
    CHECK_EQ(PADEL_STATUS_PACKET_SIZE, 6);
    CHECK_EQ(PADEL_DEVICE_INFO_SIZE, 8);
    CHECK_EQ(PADEL_CONTROL_PACKET_SIZE, 17);
    CHECK_EQ(PADEL_TOKEN_LEN, 16);

    /* Il pacchetto dello stato deve entrare nella notifica piu' piccola che
       esista (MTU 23 meno tre byte di intestazione), altrimenti servirebbe una
       negoziazione che Web Bluetooth non garantisce. */
    CHECK(PADEL_SCORE_PACKET_SIZE <= 20);

    test_end(name);
}

static void test_codifica_stato_byte_per_byte(void)
{
    const char *name = "protocollo: lo stato della partita finisce dove deve";
    test_begin(name);

    padel_score_packet_t packet = { 0 };
    packet.flags = PADEL_FLAG_TIE_BREAK | PADEL_FLAG_SERVING_NOI;
    packet.winner = PADEL_WINNER_NONE;
    packet.sequence = 0x1234u;
    packet.points[0] = 3u;    /* LORO a 40      */
    packet.points[1] = 4u;    /* NOI in vantaggio */
    packet.games[0] = 5u;
    packet.games[1] = 6u;
    packet.sets[0] = 1u;
    packet.sets[1] = 2u;
    packet.tb_points[0] = 6u;
    packet.tb_points[1] = 7u;

    uint8_t buffer[PADEL_SCORE_PACKET_SIZE];
    CHECK_EQ(padel_score_encode(&packet, buffer, sizeof(buffer)), PADEL_SCORE_PACKET_SIZE);

    CHECK_EQ(buffer[0], PADEL_PROTOCOL_VERSION);
    CHECK_EQ(buffer[1], PADEL_MSG_SCORE_STATE);
    CHECK_EQ(buffer[2], 0x05);   /* tie-break + serve NOI */
    CHECK_EQ(buffer[3], 0xFF);   /* nessun vincitore      */
    CHECK_EQ(buffer[4], 0x34);   /* il byte basso per primo */
    CHECK_EQ(buffer[5], 0x12);
    CHECK_EQ(buffer[6], 3);      /* punti LORO */
    CHECK_EQ(buffer[7], 4);      /* punti NOI  */
    CHECK_EQ(buffer[8], 5);      /* game LORO  */
    CHECK_EQ(buffer[9], 6);      /* game NOI   */
    CHECK_EQ(buffer[10], 1);     /* set LORO   */
    CHECK_EQ(buffer[11], 2);     /* set NOI    */
    CHECK_EQ(buffer[12], 6);     /* tie-break LORO, sedici bit */
    CHECK_EQ(buffer[13], 0);
    CHECK_EQ(buffer[14], 7);     /* tie-break NOI */
    CHECK_EQ(buffer[15], 0);

    test_end(name);
}

static void test_andata_e_ritorno(void)
{
    const char *name = "protocollo: quello che si scrive si rilegge uguale";
    test_begin(name);

    padel_score_packet_t original = { 0 };
    original.flags = PADEL_FLAG_FINISHED;
    original.winner = 1u;
    original.sequence = 65535u;
    original.points[0] = 4u;
    original.points[1] = 3u;
    original.games[0] = 7u;
    original.games[1] = 6u;
    original.sets[0] = 3u;
    original.sets[1] = 2u;
    original.tb_points[0] = 102u;
    original.tb_points[1] = 100u;

    uint8_t buffer[PADEL_SCORE_PACKET_SIZE];
    CHECK_EQ(padel_score_encode(&original, buffer, sizeof(buffer)), PADEL_SCORE_PACKET_SIZE);

    padel_score_packet_t read_back;
    CHECK(padel_score_decode(buffer, sizeof(buffer), &read_back));

    CHECK_EQ(read_back.flags, original.flags);
    CHECK_EQ(read_back.winner, original.winner);
    CHECK_EQ(read_back.sequence, original.sequence);
    CHECK_EQ(read_back.points[0], original.points[0]);
    CHECK_EQ(read_back.points[1], original.points[1]);
    CHECK_EQ(read_back.games[0], original.games[0]);
    CHECK_EQ(read_back.games[1], original.games[1]);
    CHECK_EQ(read_back.sets[0], original.sets[0]);
    CHECK_EQ(read_back.sets[1], original.sets[1]);
    CHECK_EQ(read_back.tb_points[0], original.tb_points[0]);
    CHECK_EQ(read_back.tb_points[1], original.tb_points[1]);

    test_end(name);
}

static void test_stato_rifiutato_se_non_valido(void)
{
    const char *name = "protocollo: uno stato non valido si rifiuta";
    test_begin(name);

    padel_score_packet_t packet = { 0 };
    uint8_t buffer[PADEL_SCORE_PACKET_SIZE];
    CHECK_EQ(padel_score_encode(&packet, buffer, sizeof(buffer)), PADEL_SCORE_PACKET_SIZE);

    padel_score_packet_t read_back;

    /* Versione diversa. */
    buffer[0] = PADEL_PROTOCOL_VERSION + 1u;
    CHECK(!padel_score_decode(buffer, sizeof(buffer), &read_back));
    buffer[0] = PADEL_PROTOCOL_VERSION;

    /* Tipo di messaggio diverso: questo e' uno stato di commissioning. */
    buffer[1] = PADEL_MSG_COMMISSIONING_STATUS;
    CHECK(!padel_score_decode(buffer, sizeof(buffer), &read_back));
    buffer[1] = PADEL_MSG_SCORE_STATE;

    /* Pacchetto tagliato. */
    CHECK(!padel_score_decode(buffer, sizeof(buffer) - 1u, &read_back));

    /* Un byte in piu' invece si accetta: una versione futura potrebbe aver
       aggiunto un campo in coda, e quelli che conosciamo sono al loro posto. */
    uint8_t larger[PADEL_SCORE_PACKET_SIZE + 4u];
    for (size_t i = 0; i < sizeof(larger); ++i) {
        larger[i] = (i < sizeof(buffer)) ? buffer[i] : 0xAAu;
    }
    CHECK(padel_score_decode(larger, sizeof(larger), &read_back));

    /* Puntatori assenti. */
    CHECK(!padel_score_decode(NULL, sizeof(buffer), &read_back));
    CHECK(!padel_score_decode(buffer, sizeof(buffer), NULL));
    CHECK_EQ(padel_score_encode(&packet, buffer, PADEL_SCORE_PACKET_SIZE - 1u), 0);
    CHECK_EQ(padel_score_encode(NULL, buffer, sizeof(buffer)), 0);

    test_end(name);
}

static void test_codifica_stato_commissioning(void)
{
    const char *name = "protocollo: lo stato dell'associazione";
    test_begin(name);

    padel_status_packet_t status = { 0 };
    status.state = (uint8_t)PADEL_COMM_WINDOW_OPEN;
    status.authenticated = 0u;
    status.result = (uint8_t)PADEL_RESULT_TIMEOUT;
    status.remaining_s = 47u;

    uint8_t buffer[PADEL_STATUS_PACKET_SIZE];
    CHECK_EQ(padel_status_encode(&status, buffer, sizeof(buffer)), PADEL_STATUS_PACKET_SIZE);
    CHECK_EQ(buffer[0], PADEL_PROTOCOL_VERSION);
    CHECK_EQ(buffer[1], PADEL_MSG_COMMISSIONING_STATUS);
    CHECK_EQ(buffer[2], PADEL_COMM_WINDOW_OPEN);
    CHECK_EQ(buffer[3], 0);
    CHECK_EQ(buffer[4], PADEL_RESULT_TIMEOUT);
    CHECK_EQ(buffer[5], 47);

    padel_status_packet_t read_back;
    CHECK(padel_status_decode(buffer, sizeof(buffer), &read_back));
    CHECK_EQ(read_back.state, status.state);
    CHECK_EQ(read_back.authenticated, status.authenticated);
    CHECK_EQ(read_back.result, status.result);
    CHECK_EQ(read_back.remaining_s, status.remaining_s);

    buffer[1] = PADEL_MSG_DEVICE_INFO;
    CHECK(!padel_status_decode(buffer, sizeof(buffer), &read_back));

    test_end(name);
}

static void test_codifica_info_scheda(void)
{
    const char *name = "protocollo: chi e' la scheda";
    test_begin(name);

    padel_device_info_packet_t info = { 0 };
    info.state = (uint8_t)PADEL_COMM_COMMISSIONED;
    info.authenticated = 1u;
    info.firmware = PADEL_FIRMWARE_VERSION;
    info.short_id = 0xA31Fu;

    uint8_t buffer[PADEL_DEVICE_INFO_SIZE];
    CHECK_EQ(padel_device_info_encode(&info, buffer, sizeof(buffer)), PADEL_DEVICE_INFO_SIZE);
    CHECK_EQ(buffer[0], PADEL_PROTOCOL_VERSION);
    CHECK_EQ(buffer[1], PADEL_MSG_DEVICE_INFO);
    CHECK_EQ(buffer[2], PADEL_COMM_COMMISSIONED);
    CHECK_EQ(buffer[3], 1);
    CHECK_EQ(buffer[4], 0x00);   /* versione firmware, byte basso */
    CHECK_EQ(buffer[5], 0x01);
    CHECK_EQ(buffer[6], 0x1F);   /* nome breve, byte basso per primo */
    CHECK_EQ(buffer[7], 0xA3);

    padel_device_info_packet_t read_back;
    CHECK(padel_device_info_decode(buffer, sizeof(buffer), &read_back));
    CHECK_EQ(read_back.firmware, PADEL_FIRMWARE_VERSION);
    CHECK_EQ(read_back.short_id, 0xA31Fu);

    test_end(name);
}

static void test_decodifica_comandi(void)
{
    const char *name = "protocollo: i comandi di associazione";
    test_begin(name);

    padel_control_packet_t command = { 0 };
    command.opcode = (uint8_t)PADEL_OP_CLAIM;
    for (uint8_t i = 0; i < PADEL_TOKEN_LEN; ++i) {
        command.token[i] = (uint8_t)(i + 1u);
    }

    uint8_t buffer[PADEL_CONTROL_PACKET_SIZE];
    CHECK_EQ(padel_control_encode(&command, buffer, sizeof(buffer)), PADEL_CONTROL_PACKET_SIZE);
    CHECK_EQ(buffer[0], PADEL_OP_CLAIM);

    padel_control_packet_t read_back;
    CHECK(padel_control_decode(buffer, sizeof(buffer), &read_back));
    CHECK_EQ(read_back.opcode, PADEL_OP_CLAIM);
    for (uint8_t i = 0; i < PADEL_TOKEN_LEN; ++i) {
        CHECK_EQ(read_back.token[i], i + 1u);
    }

    /* Anche AUTH si legge. */
    buffer[0] = PADEL_OP_AUTH;
    CHECK(padel_control_decode(buffer, sizeof(buffer), &read_back));
    CHECK_EQ(read_back.opcode, PADEL_OP_AUTH);

    /* Opcode sconosciuto. */
    buffer[0] = 0x7Fu;
    CHECK(!padel_control_decode(buffer, sizeof(buffer), &read_back));

    /* Lunghezze sbagliate: i comandi non hanno campi che possano crescere, e
       quindi non esistono versioni future piu' lunghe da accettare. */
    CHECK(!padel_control_decode(buffer, PADEL_CONTROL_PACKET_SIZE - 1u, &read_back));
    CHECK(!padel_control_decode(buffer, PADEL_CONTROL_PACKET_SIZE + 1u, &read_back));
    CHECK(!padel_control_decode(NULL, PADEL_CONTROL_PACKET_SIZE, &read_back));

    test_end(name);
}

/* -------------------------------------------------------------------------- */

void test_ble_protocol_all(void)
{
    printf("Protocollo BLE\n");

    test_dimensioni();
    test_codifica_stato_byte_per_byte();
    test_andata_e_ritorno();
    test_stato_rifiutato_se_non_valido();
    test_codifica_stato_commissioning();
    test_codifica_info_scheda();
    test_decodifica_comandi();

    printf("\n");
}
