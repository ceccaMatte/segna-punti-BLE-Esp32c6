/**
 * @file rgb_led.h
 * @brief Il LED RGB della scheda: un WS2812B pilotato dal periferico RMT.
 *
 * Un LED solo, quindi non serve nessuna libreria di strisce: i ventiquattro bit
 * che compongono un colore si scrivono direttamente nella memoria del
 * periferico, uno per simbolo, e si avvia la trasmissione. Il protocollo e' una
 * sequenza di impulsi, lunghi o corti a seconda che il bit sia uno o zero, con
 * una pausa finale che dice al LED che il colore e' completo.
 *
 * Il piedino e' GPIO8, lo stesso del progetto di esempio del produttore. E'
 * anche un piedino che il chip legge all'accensione per decidere come partire,
 * quindi va lasciato in pace finche' il chip non ha finito di avviarsi: per
 * questo il modulo si inizializza dopo lo schermo, a partenza avvenuta.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"

#include "led_anim.h"

/**
 * @brief Prepara il piedino e il periferico.
 *
 * Non manda nessun colore: dopo questa chiamata il LED resta spento finche'
 * qualcuno non ne manda uno.
 *
 * @param gpio piedino del LED.
 */
esp_err_t rgb_led_init(int gpio);

/** Vero se il LED e' pronto a ricevere colori. */
bool rgb_led_ready(void);

/** Manda un colore al LED. Senza effetto se rgb_led_init() non e' riuscita. */
void rgb_led_set(const led_rgb_t *colour);
