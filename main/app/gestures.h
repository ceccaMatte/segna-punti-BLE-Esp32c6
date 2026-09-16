/**
 * @file gestures.h
 * @brief Il pulsante di gioco: leggerlo, capire il gesto, e dirlo a chi tocca.
 *
 * Un solo pulsante, quello di BOOT, e sette gesti che convivono:
 *
 *   1 click         punto a NOI
 *   2 click         punto a LORO
 *   3 click         annulla l'ultima azione
 *   4 click         azzera la partita e ricomincia
 *   5 click o piu'  nessuna azione
 *   pressione fra 0,8 e 5 secondi, rilasciata   un segno nel tempo (MOMENT)
 *   pressione oltre 5 secondi                   apre il commissioning
 *
 * Il riconoscimento non si fa qui: sta in `button.c`, che e' pura logica e si
 * prova sul PC. Qui c'e' il mestiere che tocca al ciclo principale: leggere il
 * piedino, far avanzare la macchina a stati, e portare il gesto a destinazione.
 *
 * E la destinazione non e' sempre il gioco. Le due pressioni lunghe non sono
 * gesti di padel: una chiede un segno nel tempo, l'altra apre la finestra di
 * commissioning. Non passano dal controller e non finiscono nel punteggio; il
 * motore del gioco non sa nemmeno che esistano.
 *
 * I gesti di gioco, invece, passano dal controller e da li' allo stato
 * pubblicato: gestures.c e' il posto in cui gesto, punteggio e radio si
 * incontrano, ed e' l'unico. Chi pubblica lo fa con il nome del gesto
 * (::padel_event_t), cosi' la pagina web riceve lo stato *e* il motivo per cui
 * e' cambiato.
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

/**
 * @brief Da quanto dura la pressione in corso, in millisecondi.
 *
 * Zero quando il pulsante e' a riposo. Serve all'avviso a video: chi tiene
 * premuto deve poter vedere che il gesto e' cominciato e quanto manca alla
 * soglia del commissioning, senza scoprirlo dal rilascio.
 */
uint32_t gestures_hold_ms(void);

/**
 * @brief La soglia che apre la finestra di commissioning, in millisecondi.
 *
 * Serve all'avviso a video per riempire la sua barra: la soglia sta nel
 * riconoscitore del pulsante, e chi disegna non deve conoscerla di suo.
 */
uint32_t gestures_pairing_hold_ms(void);
