/**
 * @file display.h
 * @brief Pannello ST7789 da 172x320: inizializzazione e aggiornamento.
 *
 * E' l'unico modulo del progetto che parla direttamente con l'hardware video.
 * Tiene in memoria l'immagine completa dello schermo (un pixel per punto, in
 * RGB565) e sa mandarne al pannello solo una parte: e' questo che permette di
 * ridisegnare un punteggio senza toccare il resto.
 *
 * Chi disegna non chiama mai queste funzioni direttamente: passa dal modulo
 * grafico, che scrive nel buffer. Qui si copia quello che serve verso il
 * pannello.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/**
 * @brief Accende il pannello e prepara il buffer dell'immagine.
 *
 * Da chiamare una volta sola, all'avvio. Configura il bus SPI, il controller,
 * la retroilluminazione e svuota il buffer.
 *
 * @return ESP_OK se tutto e' andato a buon fine.
 */
esp_err_t display_init(void);

/** Vero se il pannello e' stato inizializzato correttamente. */
bool display_ready(void);

/** Larghezza del pannello in pixel. */
int display_width(void);

/** Altezza del pannello in pixel. */
int display_height(void);

/**
 * @brief Il buffer dell'immagine, in RGB565.
 *
 * Il buffer appartiene al modulo display e non va liberato. La sua estensione
 * e' ``display_width() * display_height()`` pixel, riga per riga dall'alto.
 */
uint16_t *display_pixels(void);

/**
 * @brief Manda al pannello una parte dell'immagine.
 *
 * Il rettangolo viene ritagliato su quello che il pannello puo' mostrare, quindi
 * non serve controllare i bordi prima di chiamare.
 */
void display_flush_rect(int x, int y, int w, int h);

/** Manda al pannello l'immagine intera. */
void display_flush_all(void);

/**
 * @brief Regola la luminosita' della retroilluminazione.
 *
 * @param permille da 0 a 1000. Il valore viene limitato a quello massimo
 *                 consentito per non rovinare il pannello.
 */
void display_set_backlight(int permille);

/**
 * @brief Riempie lo schermo con una figura di prova.
 *
 * Serve a verificare a occhio, appena accesa la scheda, che i colori siano
 * quelli giusti e che l'immagine non sia specchiata o tagliata.
 */
void display_test_pattern(void);
