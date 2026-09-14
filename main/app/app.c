/**
 * @file app.c
 * @brief L'avvio e il ciclo principale del segnapunti.
 *
 * Il programma fa due cose soltanto: far avanzare la partita e tenere lo
 * schermo allineato a essa. Non prende nessuna decisione sul padel: le regole
 * stanno nel motore, che non sa che esista un display, e il disegno non sa che
 * esista un pulsante. Questa separazione e' quello che permette di provare
 * tutta la logica sul computer, senza scheda collegata, con
 * ``test\run_tests.ps1``.
 *
 * il ciclo gira ogni cinque millisecondi, e cinque non e' una necessita' del
 * gioco: serve al riconoscimento dei click, che deve distinguere uno, due e tre
 * click dentro una finestra di quattrocento millisecondi.
 *
 * NOTA sull'alimentazione: il pulsante di BOOT e' anche il piedino che sceglie
 * la modalita' di avvio. Se la scheda viene accesa con il pulsante gia' premuto
 * parte il bootloader invece del segnapunti. Si preme dopo l'accensione.
 *
 * SPDX-License-Identifier: MIT
 */

#include "app.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_timer.h"

#include "banner.h"
#include "ble_score_service.h"
#include "commissioning_manager.h"
#include "commissioning_pin.h"
#include "controller.h"
#include "gestures.h"
#include "gpio_scan.h"
#include "indicators.h"
#include "screens.h"
#include "timing.h"

/* La piedinatura scritta in board.h e' quella di questa scheda: compilando per
   un altro chip il display non si accenderebbe, e senza accendersi il difetto
   sarebbe difficile da capire. Meglio fermarsi subito. */
#if !CONFIG_IDF_TARGET_ESP32C6
#error "Questa scheda monta un ESP32-C6. Esegui: idf.py set-target esp32c6"
#endif

/** Periodo del ciclo principale. Vedi il commento in testa al file. */
#define POLL_INTERVAL_MS 5

/** Il tempo che passa, misurato giro per giro. Vedi timing.h. */
static timing_t s_timing;

void app_init(void)
{
    banner_print();

    /* Prima quello che si vede e che si tocca, poi quello che parla con la
       radio: se la radio non parte, si gioca lo stesso. */
    gestures_init();
    screens_init();
    indicators_init();

    controller_init(FIRST_SERVER);

    commissioning_pin_init();

#if CONFIG_PADEL_GPIO_SCAN
    /*
     * Diagnostica: guarda tutti i piedini che la scheda porta fuori e li
     * racconta sul monitor seriale. Si accende da menuconfig, e serve a
     * distinguere "il piedino non si legge" da "il filo non arriva".
     */
    gpio_scan_init();
#endif

    /*
     * Il Bluetooth viene per ultimo: porta con se' il proprio compito e la
     * propria memoria, e se qualcosa non va il segnapunti deve continuare lo
     * stesso. Un punteggio che funziona con un Bluetooth muto e' molto meglio
     * di una scheda che si riavvia.
     */
    commissioning_manager_init();
    ble_score_service_init();

    banner_print_radio();
}

void app_run(void)
{
    timing_init(&s_timing, esp_timer_get_time());

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));

        /* Il tempo si misura davvero invece di darlo per scontato: vedi
           timing.h per il perche' e per i due limiti. */
        timing_step(&s_timing, esp_timer_get_time());

        gestures_update(s_timing.dt_ms);
        controller_tick(s_timing.dt_ms);
        indicators_update(s_timing.now_ms);
        commissioning_pin_update(s_timing.dt_ms);

#if CONFIG_PADEL_GPIO_SCAN
        gpio_scan_tick(s_timing.now_ms);
#endif

        /* Lo stato della partita si pubblica da qui, ed e' l'unico posto in cui
           il punteggio incontra la radio. */
        ble_score_service_update(controller_state(), s_timing.dt_ms);

        screens_update(s_timing.now_ms);
    }
}
