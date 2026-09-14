/**
 * Diagnostica del collegamento.
 *
 * Serve a rispondere alla domanda che si fa sempre provando una cosa nuova:
 * "ma e' affidabile?". Qui si vede se i pacchetti arrivano tutti, se qualcuno si
 * ripete, da quanto non ne arrivano e quando e' caduta la connessione. C'e'
 * anche quanto e' durata l'ultima interruzione, che e' il modo di sapere se la
 * riconnessione automatica e' davvero veloce come si vorrebbe.
 */

import type { PacketDiagnostics } from '../ble/diagnostics';
import { clear, facts } from './dom';

function ageText(ageMs: number | null): string {
  if (ageMs === null) {
    return '—';
  }
  if (ageMs < 1000) {
    return `${ageMs} ms`;
  }
  return `${(ageMs / 1000).toFixed(1)} s`;
}

/** L'ultimo tentativo di riconnessione: quanto e' durato e com'e' finito. */
function attemptText(diagnostics: PacketDiagnostics): string {
  if (diagnostics.lastAttemptMs === null) {
    return '—';
  }

  const esito = diagnostics.lastAttemptOk ? 'riuscito' : 'fallito';
  return `${ageText(diagnostics.lastAttemptMs)}, ${esito}`;
}

export class DiagnosticsPanel {
  constructor(private readonly root: HTMLElement) {}

  render(diagnostics: PacketDiagnostics, now: number): void {
    clear(this.root);

    this.root.append(
      facts([
        ['Pacchetti ricevuti', String(diagnostics.packets)],
        ['Ultimo numero', diagnostics.lastSequence === null ? '—' : String(diagnostics.lastSequence)],
        ['Salti', String(diagnostics.gaps)],
        ['Duplicati', String(diagnostics.duplicates)],
        ['Disconnessioni', String(diagnostics.disconnects)],
        ['Riconnessioni', String(diagnostics.reconnects)],
        ['Tentativi di riconnessione', String(diagnostics.attempts)],
        ['Ultimo tentativo', attemptText(diagnostics)],
        ['Ultima interruzione', ageText(diagnostics.lastOutageMs)],
        ['Battiti ricevuti', String(diagnostics.heartbeats)],
        [
          'Ultimo aggiornamento',
          diagnostics.lastUpdate === null ? '—' : new Date(diagnostics.lastUpdate).toLocaleTimeString(),
        ],
        ['Eta\' ultimo contatto', ageText(diagnostics.ageMs(now))],
        ['Ultimo errore', diagnostics.lastError ?? '—'],
      ]),
    );
  }
}
