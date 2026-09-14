/**
 * @file board.h
 * @brief Descrizione della scheda Waveshare ESP32-C6-LCD-1.47.
 *
 * Questo file contiene solo numeri e nomi: non include nulla di ESP-IDF, cosi'
 * puo' essere letto sia dal codice che gira sul chip sia da quello di supporto
 * senza tirare dentro mezzo framework.
 *
 * Il pannello e' un ST7789 da 172x320 in RGB565. La sua RAM interna e' piu'
 * grande del vetro visibile: l'immagine utile comincia a 34 pixel di distanza
 * dal bordo sinistro della RAM, quindi in orientamento verticale va dichiarato
 * un margine X di 34 e nessun margine Y.
 */
#pragma once

/* Nome mostrato a video e nei log. */
#define BOARD_NAME "ESP32-C6-LCD-1.47"

/* -------------------------------------------------------------------------- */
/* Bus SPI del display                                                        */
/* -------------------------------------------------------------------------- */

/*
 * La scheda usa il controller SPI2, l'unico libero per le periferiche: SPI0 e
 * SPI1 sono impegnati dalla memoria flash.
 *
 * Il numero del controller NON sta qui. Sembra un dettaglio da niente, ma
 * scrivere 2 al posto della costante SPI2_HOST e' un errore che non si vede
 * finche' il programma non gira sul chip: sull'ESP32-C6 SPI2_HOST vale 1,
 * perche' l'enumerazione parte da SPI1, quindi il 2 finisce su un controller
 * che su questo chip non esiste e il bus non viene creato. La costante giusta
 * si trova in driver/spi_master.h, che sta solo in display.c.
 */

#define LCD_PIN_SCLK        7
#define LCD_PIN_MOSI        6
#define LCD_PIN_CS          14
#define LCD_PIN_DC          15
#define LCD_PIN_RST         21
#define LCD_PIN_BL          22

/* 40 MHz e' il massimo che il pannello regge in modo affidabile con questi
   cavi cosi' corti. Oltre si vedono disturbi. */
#define LCD_SPI_CLOCK_HZ    (40 * 1000 * 1000)

/* Dimensione del vetro, in pixel. */
#define LCD_H_RES           172
#define LCD_V_RES           320

/* Margine da dichiarare al controller. In verticale l'offset e' X. */
#define LCD_X_GAP           34
#define LCD_Y_GAP           0

/* Il controller risponde in modo affidabile solo se la massima richiesta DMA
   sta dentro questo limite. Serve al driver SPI per dimensionare i buffer. */
#define LCD_MAX_TRANSFER    (LCD_H_RES * 80 * 2)

/* -------------------------------------------------------------------------- */
/* Pulsante BOOT                                                              */
/* -------------------------------------------------------------------------- */

/* Sulla scheda l'unico pulsante disponibile e' BOOT. E' collegato a GPIO9 ed e'
   attivo basso. GPIO9 e' anche un piedino di selezione della modalita' di
   avvio: tenerlo premuto durante l'accensione fa partire il bootloader invece
   del programma. A partita in corso non da' fastidio, ma se la scheda viene
   alimentata con il pulsante gia' premuto non parte il segnapunti. */
#define BOARD_BUTTON_GPIO   9

/* -------------------------------------------------------------------------- */
/* Retroilluminazione                                                         */
/* -------------------------------------------------------------------------- */

/* Il produttore avverte che tenere la retroilluminazione al massimo per molte
   ore lascia un alone permanente sul pannello. Si sta quindi volutamente sotto
   la meta'. */
#define LCD_BL_LEDC_CHANNEL 0
#define LCD_BL_LEDC_TIMER   0
#define LCD_BL_LEDC_FREQ_HZ 5000
#define LCD_BL_LEDC_RES_BITS 10

/* Valore in millesimi, cosi' non serve l'aritmetica in virgola mobile. */
#define LCD_BL_DEFAULT_PERMILLE 500
#define LCD_BL_MAX_PERMILLE     500

/* -------------------------------------------------------------------------- */
/* LED RGB della scheda                                                       */
/* -------------------------------------------------------------------------- */

/* La scheda monta un WS2812B su GPIO8, solo, senza altro sulla stessa linea.
   E' lo stesso piedino del progetto di esempio del produttore, quindi non ci
   sono dubbi su quale sia.

   Attenzione: GPIO8 e' uno dei piedini che il chip legge all'accensione per
   decidere come partire. Il LED ha l'ingresso ad alta impedenza e non lo
   disturba, ma il segnapunti lo configura solo a chip avviato, dopo lo schermo:
   prima non servirebbe a niente e toccarlo non porta nulla di buono. */
#define BOARD_RGB_GPIO      8
