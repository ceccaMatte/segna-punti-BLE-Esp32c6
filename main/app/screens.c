/**
 * @file screens.c
 * @brief Le tre schermate. Vedi screens.h per il perche'.
 *
 * SPDX-License-Identifier: MIT
 */

#include "screens.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"

#include "commissioning_manager.h"
#include "commissioning_ui.h"
#include "controller.h"
#include "display.h"
#include "gestures.h"
#include "hold_ui.h"
#include "ui.h"
#include "ui_view.h"

static const char *TAG = "segnapunti";

/** Per quanto resta a video la figura di prova, quando e' attiva. */
#define TEST_PATTERN_SECONDS 5

/** Vero finche' a video c'e' la schermata di commissioning. */
static bool s_commissioning_screen;

/** Vero finche' a video c'e' l'avviso della pressione lunga. */
static bool s_hold_screen;

/** La vista della partita: si riempie e si disegna a ogni giro. */
static ui_view_t s_view;

void screens_init(void)
{
    /*
     * Se lo schermo non parte, il segnapunti non viene interrotto: si annota il
     * motivo e si prosegue.
     *
     * Sembra strano, ma l'alternativa e' peggiore. Interrompere il programma
     * manda la scheda in un ciclo di riavvii, e in quel momento il monitor
     * seriale diventa inutilizzabile proprio quando serve di piu'. Andando
     * avanti, invece, il pulsante continua a funzionare e i log raccontano cosa
     * e' successo: un display spento si diagnostica, un ciclo di riavvii no.
     * Le funzioni di disegno si accorgono da sole che il pannello non c'e' e
     * non fanno nulla.
     */
    const esp_err_t error = display_init();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "schermo non disponibile (%s): si prosegue senza",
                 esp_err_to_name(error));
    } else {
        display_set_backlight(CONFIG_PADEL_BACKLIGHT_PERMILLE);
    }

#if CONFIG_PADEL_DISPLAY_TEST_PATTERN
    /*
     * Modalita' diagnostica.
     *
     * La figura viene mostrata per qualche secondo e poi lasciata al gioco.
     * Serve al primo montaggio: se le quattro barre appaiono nell'ordine
     * giusto e la squadretta bianca e' nell'angolo in alto a sinistra, allora
     * colori, margine e orientamento sono a posto.
     *
     * Non basta disegnarla e proseguire: la schermata del gioco comincia
     * proprio cancellando tutto, quindi la figura sparirebbe prima di poterla
     * vedere.
     */
    display_test_pattern();
    ESP_LOGW(TAG, "figura di prova per %d secondi", TEST_PATTERN_SECONDS);
    vTaskDelay(pdMS_TO_TICKS(TEST_PATTERN_SECONDS * 1000));
#endif

    ui_init();
    commissioning_ui_init();
    hold_ui_init();
    s_commissioning_screen = false;
    s_hold_screen = false;
}

void screens_update(uint32_t now_ms)
{
    if (commissioning_manager_screen_active()) {
        commissioning_ui_update(commissioning_manager_state(),
                                commissioning_manager_device_name(),
                                commissioning_manager_short_id(),
                                now_ms);
        s_commissioning_screen = true;
        s_hold_screen = false;
        return;
    }

    if (s_commissioning_screen) {
        /* Si torna al punteggio: la schermata di commissioning copriva tutto,
           quindi va rifatta da capo, intestazione compresa. */
        s_commissioning_screen = false;
        ui_invalidate();
    }

    /*
     * La pressione lunga e' un gesto che non si vede: finche' non si molla il
     * pulsante non succede niente, e chi lo tiene premuto non saprebbe nemmeno
     * se la scheda se ne e' accorta. Da questa soglia lo schermo lo dice, e la
     * barra mostra quanto manca al commissioning.
     */
    const uint32_t hold_ms = gestures_hold_ms();
    if (hold_ms >= CONFIG_PADEL_HOLD_HINT_MS) {
        hold_ui_update(hold_ms, gestures_pairing_hold_ms(), now_ms);
        s_hold_screen = true;
        return;
    }

    if (s_hold_screen) {
        /* Anche l'avviso copriva tutto: si rifa' tutto, intestazione
           compresa. */
        s_hold_screen = false;
        ui_invalidate();
    }

    ui_view_build(controller_state(), &s_view);
    ui_update(&s_view);

#if CONFIG_PADEL_LOG_REDRAW
    /* Le zone sono quelle che il disegno ha davvero riscritto: se l'ultimo giro
       non ha toccato niente, non c'e' niente da raccontare. */
    const int zones = ui_last_dirty_count();
    if (zones > 0) {
        ESP_LOGI(TAG, "ridisegno: %d zone, %lu pixel", zones, ui_last_dirty_pixels());
    }
#endif
}
