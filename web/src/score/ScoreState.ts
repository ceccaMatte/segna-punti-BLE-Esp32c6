/**
 * Quello che si vede sullo schermo del computer.
 *
 * E' una traduzione diretta del pacchetto ricevuto: nessun calcolo, nessun
 * passaggio da 15 a 30, nessuna idea di chi ha vinto il game. Se la scheda dice
 * 40, qui si scrive 40. E' l'unico modo perche' le due schermate non possano
 * raccontare due partite diverse.
 */

import { SIDE_LORO, SIDE_NOI, scoreLabel, type ScoreStatePacket } from '../ble/protocol';

export interface SideView {
  /** Il punteggio del game: "0", "15", "30", "40", "AD" o il numero del tie-break. */
  points: string;
  games: number;
  sets: number;
  /** Vero se tocca a questa squadra servire. */
  serving: boolean;
}

export interface ScoreView {
  loro: SideView;
  noi: SideView;
  tieBreak: boolean;
  finished: boolean;
  /** "LORO" o "NOI" quando la partita e' finita, altrimenti null. */
  winner: string | null;
  sequence: number;
}

export function toView(packet: ScoreStatePacket): ScoreView {
  const side = (index: 0 | 1): SideView => ({
    points: scoreLabel(packet, index),
    games: packet.games[index],
    sets: packet.sets[index],
    serving: packet.servingNoi ? index === SIDE_NOI : index === SIDE_LORO,
  });

  let winner: string | null = null;
  if (packet.finished && packet.winner !== null) {
    winner = packet.winner === SIDE_NOI ? 'NOI' : 'LORO';
  }

  return {
    loro: side(SIDE_LORO),
    noi: side(SIDE_NOI),
    tieBreak: packet.tieBreak,
    finished: packet.finished,
    winner,
    sequence: packet.sequence,
  };
}
