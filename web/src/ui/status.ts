/**
 * Come si racconta il collegamento, in un posto solo.
 *
 * La stessa domanda — "sto vedendo la scheda o no?" — si fa in due punti della
 * pagina, e i due devono rispondere la stessa cosa: se la banda in alto scrive
 * "collegata" e il pannello sotto scrive "non collegata", non si sa piu' a chi
 * credere. Qui c'e' la regola, senza niente che assomigli a una pagina web, ed
 * e' quindi una delle cose che si provano sul computer.
 *
 * Tre stati, non di piu':
 *
 *   ok     la scheda e' collegata e ha riconosciuto questa pagina
 *   warn   il collegamento c'e' ma manca ancora qualcosa (associazione,
 *          riconoscimento, richiesta in corso)
 *   off    non c'e' nessun collegamento
 */

export type StatusKind = 'ok' | 'warn' | 'off';

/** Quello che si sa del collegamento. */
export interface StatusFacts {
  /** Vero se il browser ha Web Bluetooth. */
  supported: boolean;
  /** Vero se il collegamento alla scheda e' aperto adesso. */
  connected: boolean;
  /** Vero se la scheda ha riconosciuto questa pagina. */
  authenticated: boolean;
  /** Nome della scheda collegata adesso, se c'e'. */
  connectedName: string | null;
  /** Nome della scheda ricordata dal browser, se ce n'e' una. */
  knownName: string | null;
  /** Vero mentre una richiesta e' in corso. */
  busy: boolean;
}

/** Quello che si sa dei dati che arrivano. */
export interface DataFacts {
  /** Eta' dell'ultimo aggiornamento ricevuto, in ms, o null se non ne sono arrivati. */
  ageMs: number | null;
  /** Secondi che restano alla finestra di commissioning, se e' aperta. */
  remainingSeconds: number | null;
}

export interface StatusHeadline {
  kind: StatusKind;
  text: string;
}

/** La riga grossa: si legge quella e si sa come stanno le cose. */
export function headline(facts: StatusFacts): StatusHeadline {
  if (!facts.supported) {
    return { kind: 'off', text: 'Web Bluetooth non disponibile in questo browser' };
  }

  if (facts.busy) {
    return { kind: 'warn', text: 'Collegamento in corso...' };
  }

  if (!facts.connected) {
    return facts.knownName !== null
      ? { kind: 'off', text: `NON COLLEGATA — ultima scheda: ${facts.knownName}` }
      : { kind: 'off', text: 'NESSUNA SCHEDA COLLEGATA' };
  }

  const nome = facts.connectedName ?? '(senza nome)';

  if (!facts.authenticated) {
    return { kind: 'warn', text: `COLLEGATA a ${nome} — non ancora riconosciuta` };
  }

  return { kind: 'ok', text: `COLLEGATA a ${nome}` };
}

/** La riga sotto: cosa fare adesso, o cosa sta arrivando. */
export function detailLine(facts: StatusFacts, data: DataFacts): string {
  if (!facts.supported) {
    return 'Servono Chrome o Edge su computer, con la pagina aperta da localhost o in HTTPS.';
  }

  if (data.remainingSeconds !== null && data.remainingSeconds > 0) {
    return `Commissioning aperto sulla scheda: restano ${data.remainingSeconds} s per associarla.`;
  }

  if (!facts.connected) {
    return facts.knownName !== null
      ? 'Premi RICONNETTI per riprendere la scheda gia\' associata a questa pagina.'
      : 'Premi COMMISSIONA SCHEDA e scegli, nella finestra del browser, la scheda da associare.';
  }

  if (!facts.authenticated) {
    return 'La scheda non ha riconosciuto questa pagina: il punteggio non arriva finche\' l\'associazione non e\' fatta.';
  }

  if (data.ageMs === null) {
    return 'In attesa del primo aggiornamento dalla scheda.';
  }

  return `Il punteggio arriva dalla scheda: ultimo aggiornamento ${formatAge(data.ageMs)} fa.`;
}

/**
 * Una durata, corta da leggere.
 *
 * Sotto il secondo si contano i millisecondi, perche' li' la differenza fra
 * "collegato" e "non collegato" si vede proprio in quella cifra; sopra il
 * minuto i decimi non servono a nessuno.
 */
export function formatAge(ageMs: number): string {
  const ms = Math.max(0, ageMs);

  if (ms < 1000) {
    return `${Math.round(ms)} ms`;
  }

  if (ms < 60000) {
    return `${(ms / 1000).toFixed(1)} s`;
  }

  if (ms < 3600000) {
    return `${Math.floor(ms / 60000)} min`;
  }

  return `${Math.floor(ms / 3600000)} h`;
}
