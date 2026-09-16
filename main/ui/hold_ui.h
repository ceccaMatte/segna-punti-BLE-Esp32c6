/**
 * @file hold_ui.h
 * @brief La schermata che invita a tenere premuto per il commissioning.
 *
 * Compare al posto del punteggio quando la pressione lunga ha superato la
 * soglia dell'avviso, e sparisce al rilascio (o lascia il posto alla
 * schermata di commissioning, se la soglia arriva). Senza, chi tiene premuto
 * non saprebbe che il gesto e' cominciato: lo scoprirebbe solo al rilascio,
 * quando ormai e' tardi.
 *
 * Non conosce il pulsante e non conosce il commissioning: riceve due numeri —
 * da quanto si sta premendo e quanto e' lunga la strada — e li racconta. Chi
 * decide *quando* mostrarla e' `screens.c`, leggendo la pressione da
 * `gestures.c`.
 *
 * La schermata e' a tutto schermo e viene ridisegnata solo quando cambia
 * qualcosa da vedere: il battito del titolo e il riempimento della barra.
 * Anche qua, come nella schermata di commissioning, quando si torna al
 * punteggio bisogna rifare tutto: questa copriva lo schermo intero.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdint.h>

/** Prepara il disegno. Da chiamare dopo display_init(). */
void hold_ui_init(void);

/**
 * @brief Mostra l'avviso, se c'e' qualcosa di nuovo da vedere.
 *
 * @param hold_ms      da quanto il pulsante e' premuto.
 * @param threshold_ms la soglia che aprira' il commissioning: la barra si
 *                     riempie in proporzione a questa.
 * @param now_ms       millisecondi dall'avvio: il titolo lampeggia con quelli.
 */
void hold_ui_update(uint32_t hold_ms, uint32_t threshold_ms, uint32_t now_ms);

/** Obbliga a ridisegnare la prossima volta. */
void hold_ui_invalidate(void);
