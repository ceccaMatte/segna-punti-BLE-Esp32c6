/**
 * @file gestures.h
 * @brief Il pulsante di gioco: leggerlo, capire il gesto, e dirlo a chi tocca.
 *
 * Un solo pulsante, quello di BOOT, e sei gesti che convivono:
 *
 *   1 click         punto a NOI
 *   2 click         punto a LORO
 *   3 click         annulla l'ultima azione
 *   4 click o piu'  nessuna azione
 *   pressione 4 s   azzera la partita e ricomincia
 *   pressione 6 s   apre la finestra di commissioning
 *
 * Il riconoscimento non si fa qui: sta in `button.c`, che e' pura logica e si
 * prova sul PC. Qui c'e' il mestiere che tocca al ciclo principale: leggere il
 * piedino, far avanzare la macchina a stati, e portare l'evento a destinazione.
 *
 * E la destinazione non e' sempre il gioco. La pressione piu' lunga non e' un
 * gesto di padel: apre il commissioning, quindi non passa dal controller e non
 * finisce nel punteggio. Il motore del gioco non sa nemmeno che esista.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdint.h>

/** Prepara il piedino del pulsante e la macchina dei gesti. */
void gestures_init(void);

/**
 * @brief Fa avanzare il pulsante e smista quello che ne esce.
 *
 * Da chiamare a ogni giro del ciclo principale.
 *
 * @param dt_ms millisecondi trascorsi dall'ultima chiamata.
 */
void gestures_update(uint32_t dt_ms);
