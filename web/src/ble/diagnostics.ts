/**
 * Diagnostica del flusso di notifiche.
 *
 * Serve a rispondere a una domanda sola: le notifiche arrivano tutte? Il numero
 * di sequenza che la scheda mette in ogni pacchetto lo dice, purche' qualcuno lo
 * guardi. Un salto significa che qualcosa si e' perso; un numero ripetuto
 * significa che e' arrivato due volte.
 *
 * Le statistiche si azzerano quando ci si collega: altrimenti i cambiamenti
 * avvenuti mentre la pagina era chiusa sembrerebbero pacchetti persi, e non lo
 * sono.
 */

import type { PadelEvent } from './protocol';

export class PacketDiagnostics {
  /** Pacchetti accettati e mostrati. */
  packets = 0;

  /** Ultimo numero di sequenza visto, null se non ne e' arrivato ancora nessuno. */
  lastSequence: number | null = null;

  /**
   * L'ultimo gesto raccontato dalla scheda, e quando e' arrivato.
   *
   * Non e' la stessa cosa dell'ultimo pacchetto: i battiti non lo cambiano, e
   * nemmeno le sincronizzazioni dopo una connessione. E' il dato da guardare
   * per rispondere alla domanda che si fa premendo un pulsante: "e' arrivato?".
   */
  lastEvent: PadelEvent | null = null;
  lastEventAt: number | null = null;

  /** Gli ultimi byte arrivati, in esadecimale: il pacchetto come sta sulla radio. */
  lastPacketHex: string | null = null;

  /** Numeri saltati: quanti aggiornamenti non sono arrivati. */
  gaps = 0;

  /** Pacchetti arrivati con lo stesso numero del precedente. */
  duplicates = 0;

  /** Quante volte la connessione e' caduta. */
  disconnects = 0;

  /** Quante volte il collegamento e' tornato dopo essere caduto. */
  reconnects = 0;

  /** Tentativi di riconnessione fatti da quando il collegamento e' tornato. */
  attempts = 0;

  /**
   * Quanto e' durato l'ultimo tentativo di riconnessione, in millisecondi.
   *
   * La durata dice piu' cose del numero: un tentativo che torna subito vuol
   * dire che il computer ha risposto di no, uno che ci mette cinque secondi
   * vuol dire che e' rimasto in attesa. Sono due problemi diversi, e si
   * aggiustano in due modi diversi.
   */
  lastAttemptMs: number | null = null;

  /** Vero se l'ultimo tentativo di riconnessione e' riuscito. */
  lastAttemptOk = false;

  /**
   * Quanto e' durata l'ultima interruzione, in millisecondi.
   *
   * E' il numero che dice se la riconnessione automatica sta facendo il suo
   * mestiere: se la scheda si riavvia in due secondi, la pagina non dovrebbe
   * metterci molto di piu' a tornare a mostrare il punteggio.
   */
  lastOutageMs: number | null = null;

  /** Quando e' caduto il collegamento, se e' caduto e non e' ancora tornato. */
  private disconnectedAt: number | null = null;
  /** Quanti battiti sono arrivati: la scheda che dice "ci sono" senza novita'. */
  heartbeats = 0;

  /**
   * Quando si e' sentita la scheda l'ultima volta, pacchetto o battito che sia.
   *
   * E' la misura del contatto, non degli aggiornamenti: con i battiti che
   * arrivano ogni secondo, un numero grande qui vuol dire che la scheda non
   * c'e' piu' — ed e' quello che fa scattare la riconnessione.
   */
  lastUpdate: number | null = null;

  /** L'ultimo errore visto, se ce n'e' uno. */
  lastError: string | null = null;

  /** Riporta tutto a zero. Si chiama quando si apre una connessione nuova. */
  reset(): void {
    this.packets = 0;
    this.lastSequence = null;
    this.lastEvent = null;
    this.lastEventAt = null;
    this.lastPacketHex = null;
    this.gaps = 0;
    this.duplicates = 0;
    this.lastUpdate = null;
    this.lastError = null;
  }

  /**
   * Registra un pacchetto ricevuto.
   *
   * @param sequence numero di sequenza del pacchetto.
   * @param now      istante di arrivo, in millisecondi.
   * @param raw      i byte come sono arrivati, in esadecimale. Assente per le
   *                 letture, che non passano dalla radio come notifica.
   */
  notePacket(sequence: number, now: number, raw: string | null = null): void {
    if (this.lastSequence === null) {
      /* Primo pacchetto dopo la connessione: si prende come punto di partenza,
         senza contare niente. */
      this.lastSequence = sequence;
    } else if (sequence === this.lastSequence) {
      this.duplicates += 1;
    } else {
      /* La sequenza e' a sedici bit e gira: si confronta con l'aritmetica
         circolare, altrimenti dopo 65535 pacchetti si vedrebbe un salto
         enorme. */
      const advanced = (sequence - this.lastSequence + 0x10000) % 0x10000;
      if (advanced === 0) {
        this.duplicates += 1;
      } else if (advanced > 1) {
        this.gaps += advanced - 1;
        this.lastSequence = sequence;
      } else {
        this.lastSequence = sequence;
      }
    }

    this.packets += 1;
    this.lastUpdate = now;

    if (raw !== null) {
      this.lastPacketHex = raw;
    }
  }

  /**
   * Registra il motivo per cui la scheda ha mandato il pacchetto.
   *
   * @param event il gesto, o l'evento di servizio.
   * @param now   istante di arrivo, in millisecondi.
   */
  noteEvent(event: PadelEvent, now: number): void {
    this.lastEvent = event;
    this.lastEventAt = now;
  }

  /**
   * Registra un battito: la scheda c'e', ma non ha niente da dire.
   *
   * Non e' un pacchetto e non tocca i numeri di sequenza, i salti e i
   * duplicati: serve a una cosa sola, e importante, tenere viva la misura di
   * quando si e' sentita l'ultima volta.
   */
  noteHeartbeat(now: number): void {
    this.heartbeats += 1;
    this.lastUpdate = now;
  }

  noteDisconnect(now: number): void {
    this.disconnects += 1;
    this.disconnectedAt = now;
  }

  /** Registra un tentativo di riconnessione e quanto e' durato. */
  noteAttempt(durationMs: number, ok: boolean): void {
    this.attempts += 1;
    this.lastAttemptMs = durationMs;
    this.lastAttemptOk = ok;
  }

  /**
   * Registra che il collegamento e' tornato.
   *
   * @return quanto e' durata l'interruzione, o null se non c'era niente da
   *         chiudere (prima connessione della pagina, o caduta mai vista).
   */
  noteReconnect(now: number): number | null {
    if (this.disconnectedAt === null) {
      return null;
    }

    const outage = now - this.disconnectedAt;
    this.disconnectedAt = null;
    this.reconnects += 1;
    this.lastOutageMs = outage;

    /* Il conto dei tentativi vale per l'interruzione appena chiusa: se ne
       ricomincia a contare dalla prossima. */
    this.attempts = 0;

    return outage;
  }

  noteError(message: string): void {
    this.lastError = message;
  }

  /** Da quanti millisecondi non arriva niente. */
  ageMs(now: number): number | null {
    return this.lastUpdate === null ? null : now - this.lastUpdate;
  }
}
