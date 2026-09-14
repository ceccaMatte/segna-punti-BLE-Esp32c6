/**
 * La riconnessione automatica.
 *
 * Le cuffie Bluetooth fanno cosi': le accendi e si ricollegano da sole
 * all'ultimo telefono, senza che nessuno apra un elenco e scelga. Questa pagina
 * fa lo stesso con la scheda: se il collegamento cade — perche' la scheda si e'
 * riavviata, perche' e' stato riflashato, o perche' e' passato un momento di
 * disturbo — riprova da sola, e si ferma appena riesce o appena qualcuno le dice
 * di fermarsi.
 *
 * Quello che non puo' fare, e conviene saperlo: dopo un *ricaricamento della
 * pagina* il browser puo' chiedere un click, a seconda di come e' configurato.
 * La pagina non puo' aggirarlo — e' una regola del browser, non una svista —
 * quindi lo dice invece di provarci per sempre.
 *
 * La politica dei tempi sta qui, lontana dal browser: cosi' si prova sul
 * computer, che e' l'unico modo di verificare che il primo tentativo arrivi
 * subito e che al decimo non si continui a martellare la radio.
 *
 * Perche' i tempi si allargano: se la scheda e' spenta non serve riprovare ogni
 * secondo. Perche' c'e' un tetto a quindici secondi: se e' accesa e lontana,
 * riprovare con calma non da' noia a nessuno e non consuma la batteria del
 * computer.
 */

/** Quanto si aspetta prima di riprovare, tentativo dopo tentativo. */
export const RETRY_DELAYS_MS = [1000, 2000, 5000, 10000, 15000] as const;

/**
 * @param failedAttempts quanti tentativi sono gia' falliti.
 * @return quanto aspettare prima del prossimo.
 */
export function retryDelayMs(failedAttempts: number): number {
  const index = Math.min(Math.max(failedAttempts, 0), RETRY_DELAYS_MS.length - 1);
  return RETRY_DELAYS_MS[index];
}

/** Cosa sta facendo la riconnessione, in questo momento. */
export interface RetryView {
  /** Vero se la pagina sta riprovando da sola. */
  enabled: boolean;
  /** Vero nell'istante in cui un tentativo e' in corso. */
  attempting: boolean;
  /** Tentativi fatti finora. */
  attempts: number;
  /** Quando partira' il prossimo, in millisecondi da epoch; null se non c'e' un prossimo. */
  nextAttemptAtMs: number | null;
}

export interface AutoReconnectOptions {
  /** Un tentativo di collegamento: true se il collegamento e' aperto. */
  attempt: () => Promise<boolean>;
  /** Chiamata a ogni cambiamento, per ridisegnare la pagina. */
  onChange: () => void;
}

export class AutoReconnect {
  private readonly attempt: () => Promise<boolean>;
  private readonly onChange: () => void;

  private handle: ReturnType<typeof setTimeout> | null = null;
  private enabled = false;
  private attempting = false;
  private attempts = 0;
  private nextAttemptAtMs: number | null = null;

  constructor(options: AutoReconnectOptions) {
    this.attempt = options.attempt;
    this.onChange = options.onChange;
  }

  view(): RetryView {
    return {
      enabled: this.enabled,
      attempting: this.attempting,
      attempts: this.attempts,
      nextAttemptAtMs: this.nextAttemptAtMs,
    };
  }

  /**
   * Comincia a riprovare. Chiamarla di nuovo mentre sta gia' lavorando non fa
   * niente: serve a poterla chiamare a ogni caduta di collegamento senza dover
   * sapere se ce n'e' gia' una in corso.
   *
   * @param immediate true per tentare adesso — e' il caso della pagina appena
   *        aperta, con la scheda magari li' accesa. False per aspettare il primo
   *        intervallo, che e' il caso del collegamento appena caduto: la radio
   *        ha bisogno di un momento prima di accettare un altro tentativo.
   */
  start(immediate = true): void {
    if (this.enabled && (this.attempting || this.handle !== null)) {
      return;
    }

    this.enabled = true;
    this.attempts = 0;
    this.nextAttemptAtMs = null;

    if (immediate) {
      void this.tryNow();
    } else {
      this.planNext();
    }

    this.onChange();
  }

  /**
   * Smette di riprovare.
   *
   * E' la volonta' dell'utente — ha premuto SCOLLEGA, o ha deciso di non
   * aspettare piu' — e vale anche per un tentativo gia' in programma: non deve
   * arrivare nessuna connessione a sorpresa dopo che qualcuno ha detto basta.
   */
  stop(): void {
    if (!this.enabled) {
      return;
    }

    this.enabled = false;
    this.attempts = 0;
    this.clearTimer();
    this.onChange();
  }

  /** Il collegamento c'e': il conto dei tentativi riparte da zero. */
  connected(): void {
    this.attempts = 0;
    this.clearTimer();
  }

  /**
   * Un tentativo subito.
   *
   * Serve quando qualcosa e' cambiato fuori dalla pagina: il computer si e'
   * risvegliato, la scheda e' stata accesa adesso, l'utente e' tornato sulla
   * scheda del browser. Aspettare il turno del timer non avrebbe senso.
   */
  retryNow(): void {
    if (!this.enabled || this.attempting) {
      return;
    }

    this.clearTimer();
    void this.tryNow();
  }

  private clearTimer(): void {
    if (this.handle !== null) {
      clearTimeout(this.handle);
      this.handle = null;
    }
    this.nextAttemptAtMs = null;
  }

  private planNext(): void {
    if (!this.enabled) {
      return;
    }

    /* `attempts` conta i tentativi gia' fatti: se ne e' appena fallito uno, i
       fallimenti sono quelli meno il tentativo in corso. E' quello che vuole
       la scala dei tempi. */
    const delay = retryDelayMs(this.attempts - 1);
    this.nextAttemptAtMs = Date.now() + delay;
    this.handle = setTimeout(() => {
      this.handle = null;
      this.nextAttemptAtMs = null;
      void this.tryNow();
    }, delay);

    this.onChange();
  }

  private async tryNow(): Promise<void> {
    /* Un tentativo per volta: due `connect()` in parallelo sulla stessa scheda
       si danno fastidio a vicenda, e il secondo fallisce sempre. */
    if (!this.enabled || this.attempting) {
      return;
    }

    this.attempting = true;
    this.attempts += 1;
    this.onChange();

    let ok = false;
    try {
      ok = await this.attempt();
    } catch {
      ok = false;
    }

    this.attempting = false;

    /* Nel frattempo qualcuno puo' aver detto basta: in quel caso non si
       riprogramma niente. */
    if (!this.enabled) {
      this.onChange();
      return;
    }

    if (ok) {
      this.attempts = 0;
      this.clearTimer();
      this.onChange();
      return;
    }

    this.planNext();
  }
}
