/**
 * @file banner.h
 * @brief Il cartello che la scheda scrive sul monitor seriale appena accesa.
 *
 * Serve a rispondere, senza aprire menuconfig, alle domande che si fanno
 * guardando una scheda che non si conosce: che chip e', come e' regolata, quale
 * nome si e' data, e quali gesti accetta.
 *
 * Sta in due pezzi perche' due cose non si possono sapere subito: il nome della
 * scheda arriva dal Bluetooth, che parte per ultimo.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

/** Le righe che si possono dire appena il programma comincia. */
void banner_print(void);

/** Le due righe che aspettano che la radio sia partita. */
void banner_print_radio(void);
