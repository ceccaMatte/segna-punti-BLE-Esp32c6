/**
 * @file dirty.h
 * @brief Elenco delle zone dello schermo da ridisegnare.
 *
 * Il segnapunti non ridisegna mai lo schermo intero mentre si gioca: a ogni
 * punto cambia solo il riquadro del punteggio e, qualche volta, anche la riga
 * dei game. Questo modulo raccoglie le zone da aggiornare e le tiene in ordine,
 * fondendo quelle che si sovrappongono o che sono cosi' vicine da convenire
 * unirle.
 *
 * La fusione serve a non pagare due volte il costo di avviare una trasmissione
 * verso il pannello: mandare due rettangoli attaccati costa quasi il doppio di
 * mandarne uno solo che li contiene.
 *
 * Non c'e' nessuna dipendenza dall'hardware: si compila e si prova sul PC.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"

/**
 * Quanti rettangoli si tengono al massimo.
 *
 * Durante il gioco se ne usano uno o due: otto e' un margine abbondante che
 * copre anche il caso in cui piu' cose cambino insieme.
 */
#define DIRTY_CAPACITY 8

/**
 * Distanza entro cui due rettangoli vengono considerati vicini.
 *
 * Fra l'uno e l'altro resterebbe una striscia di pochi pixel: mandare quella
 * striscia in piu' costa meno che avviare una seconda trasmissione.
 */
#define DIRTY_MERGE_GAP 8

/** L'insieme delle zone da aggiornare. */
typedef struct {
    gfx_rect_t rects[DIRTY_CAPACITY];
    uint8_t    count;

    /**
     * Quante volte l'elenco era pieno e si e' dovuto fondere qualcosa.
     *
     * Se questo numero cresce durante una partita vuol dire che l'elenco e'
     * troppo piccolo, oppure che la schermata cambia in troppi punti distanti
     * fra loro: e' un segnale da tenere d'occhio, non un errore.
     */
    uint32_t forced_merges;
} dirty_list_t;

/** Svuota l'elenco. */
void dirty_reset(dirty_list_t *d);

/**
 * @brief Aggiunge una zona da aggiornare.
 *
 * La zona viene fusa con quelle che toccano o che distano meno di
 * ::DIRTY_MERGE_GAP, e la fusione si ripete finche' se ne trovano altre: cosi'
 * l'elenco non contiene mai due rettangoli che sarebbe stato meglio unire.
 *
 * Se i posti sono finiti si fonde la coppia che, unita, ingrandisce meno l'area
 * totale. La coppia puo' comprendere anche la zona appena arrivata: se e' quella
 * a stare piu' vicino a una zona esistente, unirla e' meglio che fondere due
 * zone gia' presenti e sistemare la nuova da sola.
 *
 * I rettangoli vuoti vengono ignorati.
 */
void dirty_add_rect(dirty_list_t *d, gfx_rect_t r);

/** Come ::dirty_add_rect, con le coordinate sciolte. */
void dirty_add(dirty_list_t *d, int x, int y, int w, int h);

/** Aggiunge una zona che copre tutto lo schermo: equivale a un ridisegno pieno. */
void dirty_add_full(dirty_list_t *d, int screen_w, int screen_h);

/** Quante zone ci sono in elenco. */
uint8_t dirty_count(const dirty_list_t *d);

/** La zona in posizione ``index``. Se l'indice non esiste restituisce una zona vuota. */
gfx_rect_t dirty_get(const dirty_list_t *d, uint8_t index);

/** Vero se non c'e' niente da aggiornare. */
bool dirty_is_empty(const dirty_list_t *d);

/** Vero se non c'e' piu' posto. */
bool dirty_is_full(const dirty_list_t *d);

/** Somma delle aree, utile per misurare quanto lavoro e' stato chiesto. */
uint32_t dirty_area(const dirty_list_t *d);
