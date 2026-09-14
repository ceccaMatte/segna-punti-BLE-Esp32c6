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

export class PacketDiagnostics {
  /** Pacchetti accettati e mostrati. */
  packets = 0;

  /** Ultimo numero di sequenza visto, null se non ne e' arrivato ancora nessuno. */
  lastSequence: number | null = null;

  /** Numeri saltati: quanti aggiornamenti non sono arrivati. */
  gaps = 0;

  /** Pacchetti arrivati con lo stesso numero del precedente. */
  duplicates = 0;

  /** Quante volte la connessione e' caduta. */
  disconnects = 0;

  /** Quando e' arrivato l'ultimo pacchetto, in millisecondi da epoch. */
  lastUpdate: number | null = null;

  /** L'ultimo errore visto, se ce n'e' uno. */
  lastError: string | null = null;

  /** Riporta tutto a zero. Si chiama quando si apre una connessione nuova. */
  reset(): void {
    this.packets = 0;
    this.lastSequence = null;
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
   */
  notePacket(sequence: number, now: number): void {
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
  }

  noteDisconnect(): void {
    this.disconnects += 1;
  }

  noteError(message: string): void {
    this.lastError = message;
  }

  /** Da quanti millisecondi non arriva niente. */
  ageMs(now: number): number | null {
    return this.lastUpdate === null ? null : now - this.lastUpdate;
  }
}
