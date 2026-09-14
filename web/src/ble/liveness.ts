/**
 * Il battito: come la pagina si accorge che la scheda non c'e' piu'.
 *
 * Il problema e' questo: quando la scheda si riavvia, o si allontana, o le
 * manca la corrente, il collegamento muore **senza avvisare**. Il sistema
 * operativo se ne accorge solo allo scadere del tempo di supervisione del
 * Bluetooth, e su un computer quel tempo e' di parecchi secondi: misurato, una
 * decina. Fino a quel momento la pagina crede di essere collegata, e non c'e'
 * niente da fare perche' e' il sistema a non sapere ancora niente.
 *
 * La via d'uscita non e' aspettare meglio: e' **non dipendere dal sistema**. La
 * scheda manda un battito ogni tanto anche quando il punteggio non cambia (vedi
 * `HEARTBEAT_MS` in `ble_score_service.c`), e la pagina conta quanto tempo e'
 * passato dall'ultimo. Due battiti mancati vogliono dire che la scheda non c'e'
 * piu': a quel punto la pagina chiude il collegamento morto — un `disconnect()`
 * esplicito lo fa cadere subito, senza aspettare la supervisione — e riprova.
 *
 * Il conto e' presto fatto: tre secondi per accorgersene, mezzo secondo per il
 * primo tentativo, uno per ricollegarsi e riconoscersi. Sotto i cinque secondi,
 * che era l'obiettivo.
 *
 * Le due costanti sono legate a quelle della scheda: se cambia il battito di la'
 * va cambiata la soglia di qua.
 */

/** Ogni quanto la scheda manda un battito. Deve combaciare col firmware. */
export const HEARTBEAT_MS = 1200;

/**
 * Quanto silenzio vuol dire "non c'e' piu'".
 *
 * Due battiti mancati piu' un margine: con uno solo, un pacchetto perso per
 * disturbo farebbe chiudere un collegamento che stava benissimo.
 */
export const SILENCE_TIMEOUT_MS = 3000;

/**
 * Il conto del silenzio.
 *
 * Tiene solo l'istante dell'ultimo segno di vita e sa dire se e' passato troppo
 * tempo. Sta da solo, senza Bluetooth e senza timer, perche' e' la parte che
 * vale la pena di provare: sbagliare qui vuol dire o non accorgersi mai della
 * caduta, o chiudere collegamenti sani.
 */
export class SilenceWatch {
  private lastAliveAt: number | null = null;

  /** La scheda si e' fatta sentire. */
  noteAlive(now: number): void {
    this.lastAliveAt = now;
  }

  /** Non si sa piu' niente: si ricomincia da capo, senza scattare. */
  reset(): void {
    this.lastAliveAt = null;
  }

  /**
   * Vero se la scheda tace da troppo tempo.
   *
   * Finche' non si e' sentita nemmeno una volta risponde falso: prima del primo
   * battito il silenzio non vuol dire niente, e chi chiama ha comunque altro da
   * mostrare ("in attesa del primo aggiornamento").
   */
  expired(now: number): boolean {
    return this.lastAliveAt !== null && now - this.lastAliveAt > SILENCE_TIMEOUT_MS;
  }

  /** Da quanti millisecondi tace, o null se non si e' mai sentita. */
  silenceMs(now: number): number | null {
    return this.lastAliveAt === null ? null : now - this.lastAliveAt;
  }
}
