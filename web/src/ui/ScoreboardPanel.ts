/**
 * Il tabellone.
 *
 * Mostra quello che ha detto la scheda, senza rifare nessun conto: se la scheda
 * scrive 40, qui si legge 40. E' voluto: due programmi che calcolano lo stesso
 * punteggio finiscono prima o poi per non essere d'accordo, e a quel punto non
 * si sa piu' a chi credere.
 */

import { clear, el } from './dom';
import type { ScoreView, SideView } from '../score/ScoreState';

function sideColumn(name: string, side: SideView, sideClass: string): HTMLElement {
  const column = el('div', `side ${sideClass}`);

  const head = el('div', 'side-head');
  head.append(el('span', 'side-name', name));

  if (side.serving) {
    const dot = el('span', 'serve-dot');
    dot.title = 'Serve questa squadra';
    head.append(dot);
  }

  const points = el('div', 'side-points', side.points);

  const counters = el('div', 'side-counters');
  counters.append(
    el('span', 'counter', `game ${side.games}`),
    el('span', 'counter', `set ${side.sets}`),
  );

  column.append(head, points, counters);
  return column;
}

export class ScoreboardPanel {
  constructor(private readonly root: HTMLElement) {}

  render(view: ScoreView | null, stale: boolean): void {
    clear(this.root);

    if (view === null) {
      this.root.append(
        el('p', 'message', 'Nessun punteggio ricevuto. Collegati alla scheda per vederlo.'),
      );
      return;
    }

    if (stale) {
      this.root.append(
        el('p', 'message', 'Ultimo punteggio ricevuto prima della disconnessione.'),
      );
    }

    const board = el('div', stale ? 'board board-stale' : 'board');
    board.append(
      sideColumn('LORO', view.loro, 'side-loro'),
      sideColumn('NOI', view.noi, 'side-noi'),
    );
    this.root.append(board);

    const notes = el('div', 'board-notes');

    if (view.tieBreak) {
      notes.append(el('span', 'badge badge-tb', 'TIE-BREAK'));
    }
    if (view.finished && view.winner !== null) {
      notes.append(el('span', 'badge badge-win', `VINCE ${view.winner}`));
    }
    notes.append(el('span', 'badge', `snapshot ${view.sequence}`));

    this.root.append(notes);
  }
}
