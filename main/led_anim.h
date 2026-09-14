/**
 * @file led_anim.h
 * @brief Lo spettacolo di luci sul LED RGB e che colore lasciare acceso.
 *
 * Modulo di pura logica: non sa che esista un LED. Riceve il tempo e restituisce
 * un colore, quindi lo spettacolo si puo' provare sul computer guardando il
 * colore istante per istante, invece di aspettare che capiti il punto giusto con
 * la scheda in mano e sperare di ricordarselo.
 *
 * Il LED racconta chi ha segnato l'ultimo punto:
 *
 *   punto     spettacolo di LED_SHOW_MS, poi resta sul colore di chi ha segnato
 *   annulla   niente spettacolo: il colore passa a chi aveva segnato prima
 *   azzera    spento, come a partita appena cominciata
 *
 * La regola dell'annullamento e' voluta: chi si e' corretto non deve vedersi una
 * festa. Il colore si riallinea e basta, che per una correzione e' quello che
 * serve.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "match.h"

/** Durata dello spettacolo dopo un punto. */
#ifndef LED_SHOW_MS
#define LED_SHOW_MS 1600u
#endif

/**
 * Luminosita' massima, in millesimi.
 *
 * Un WS2812 tenuto al massimo acceca: a mezzo metro e' fastidioso da guardare e
 * di sera illumina la stanza. Il tetto vale per tutti i colori, spettacolo
 * compreso, cosi' il lampo non e' mai piu' forte di quello che resta acceso
 * alla fine. A zero il LED e' spento.
 */
#ifndef LED_BRIGHTNESS_PERMILLE
#define LED_BRIGHTNESS_PERMILLE 400u
#endif

/** Un colore a otto bit per canale, nell'ordine rosso, verde, blu. */
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} led_rgb_t;

/** Spegne il LED e dimentica i punti segnati. */
void led_anim_init(void);

/**
 * @brief Un punto: parte lo spettacolo e alla fine resta il colore di chi ha
 * segnato.
 *
 * Se lo spettacolo precedente era ancora in corso viene sostituito: due punti
 * rapidi non si accavallano.
 */
void led_anim_point(team_t scorer, uint32_t now_ms);

/** Annullamento: nessuno spettacolo, il colore torna indietro di un punto. */
void led_anim_undo(void);

/** Partita azzerata: LED spento e memoria dei punti svuotata. */
void led_anim_reset(void);

/** Il colore da mostrare in questo istante. */
led_rgb_t led_anim_colour(uint32_t now_ms);

/** Vero se in questo istante lo spettacolo e' in corso. */
bool led_anim_showing(uint32_t now_ms);

/**
 * @brief Il colore da mandare al LED, ma solo quando e' diverso dal precedente.
 *
 * Il ciclo principale gira ogni cinque millisecondi, mentre il colore cambia
 * davvero soltanto durante lo spettacolo. Chiedendo da qui si manda al LED una
 * scrittura per ogni cambiamento, e nessuna quando e' fermo su un colore.
 *
 * @param[out] out riempito solo se il ritorno e' true.
 * @return true se il colore e' cambiato dall'ultima chiamata.
 */
bool led_anim_update(uint32_t now_ms, led_rgb_t *out);
