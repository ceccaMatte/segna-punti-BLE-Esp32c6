/**
 * @file app.h
 * @brief L'avvio e il ciclo, con la mappa di chi fa cosa.
 *
 * Il programma e' un ciclo da cinque millisecondi che non prende decisioni sul
 * padel. Ogni giro fa le stesse cose, nello stesso ordine, e ognuna e' affidata
 * al suo modulo:
 *
 *   gestures.c              il pulsante, e dove va a finire quello che dice
 *   controller.c            il tempo della partita (motore in game/)
 *   indicators.c            il LED di bordo
 *   link/commissioning_pin.c   il piedino e la finestra di commissioning
 *   link/ble_score_service.c   il punteggio che esce di casa
 *   screens.c               quello che c'e' sullo schermo
 *
 * Le regole del gioco stanno in game/, il disegno in ui/, la radio in link/,
 * l'hardware in board/. Questo file non ne sa niente di nessuna delle quattro:
 * le mette in fila.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "match.h"

/**
 * Chi serve per primo.
 *
 * Nel padel il primo servizio si sorteggia, quindi non c'e' una risposta
 * giusta: si sceglie da menuconfig.
 */
#ifdef CONFIG_PADEL_FIRST_SERVER_LORO
#define FIRST_SERVER TEAM_THEM
#else
#define FIRST_SERVER TEAM_US
#endif

/** Fa partire tutto, nell'ordine giusto: prima lo schermo, poi la radio. */
void app_init(void);

/** Il ciclo principale. Non torna mai. */
void app_run(void);
