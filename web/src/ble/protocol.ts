/**
 * Il protocollo della scheda, dal lato della pagina web.
 *
 * Gemello di `main/ble_protocol.h`: gli stessi UUID, gli stessi opcode, la
 * stessa disposizione dei byte. Le due copie devono restare d'accordo, e i test
 * di qua e di la' usano gli stessi valori di esempio, cosi' se una delle due
 * cambia si vede subito.
 *
 * Qui non si calcola niente del padel: si leggono byte e si scrivono byte. Chi
 * usa questi pacchetti si limita a mostrarli.
 */

/* -------------------------------------------------------------------------- */
/* Identificativi                                                             */
/* -------------------------------------------------------------------------- */

export const SERVICE_UUID = '6b8d0001-9c4f-4e21-b7a3-0d5e1f2a3b40';
export const DEVICE_INFO_UUID = '6b8d0002-9c4f-4e21-b7a3-0d5e1f2a3b40';
export const SCORE_STATE_UUID = '6b8d0003-9c4f-4e21-b7a3-0d5e1f2a3b40';
export const COMMISSIONING_CONTROL_UUID = '6b8d0004-9c4f-4e21-b7a3-0d5e1f2a3b40';
export const COMMISSIONING_STATUS_UUID = '6b8d0005-9c4f-4e21-b7a3-0d5e1f2a3b40';

/** Prefisso del nome con cui la scheda si annuncia. */
export const NAME_PREFIX = 'PADEL_SCORE_';

/** Versione del protocollo. Un pacchetto di un'altra versione si scarta. */
export const PROTOCOL_VERSION = 1;

/** Lunghezza del token di associazione. */
export const TOKEN_LENGTH = 16;

/** Lunghezze attese dei pacchetti. */
export const SCORE_PACKET_SIZE = 16;
export const STATUS_PACKET_SIZE = 6;
export const DEVICE_INFO_SIZE = 8;
export const CONTROL_PACKET_SIZE = 1 + TOKEN_LENGTH;

/* -------------------------------------------------------------------------- */
/* Tipi di messaggio e valori                                                 */
/* -------------------------------------------------------------------------- */

export enum MessageType {
  ScoreState = 1,
  CommissioningStatus = 2,
  DeviceInfo = 3,
}

export enum CommissioningState {
  Uncommissioned = 0,
  WindowOpen = 1,
  Commissioned = 2,
}

export enum ResultCode {
  Idle = 0,
  ClaimSuccess = 1,
  AuthSuccess = 2,
  AuthFailed = 3,
  ClaimRejected = 4,
  Timeout = 5,
  ProtocolError = 6,
}

export enum ControlOp {
  Claim = 0x01,
  Auth = 0x02,
}

/** Bit di stato del pacchetto della partita. */
export const FLAG_TIE_BREAK = 0x01;
export const FLAG_FINISHED = 0x02;
export const FLAG_SERVING_NOI = 0x04;

/** Vincitore assente. */
export const WINNER_NONE = 0xff;

/** Indici delle squadre, come nel motore della scheda. */
export const SIDE_LORO = 0;
export const SIDE_NOI = 1;

/* -------------------------------------------------------------------------- */
/* Pacchetti                                                                  */
/* -------------------------------------------------------------------------- */

export interface ScoreStatePacket {
  /** Numero dello snapshot: cresce a ogni pubblicazione. */
  sequence: number;
  tieBreak: boolean;
  finished: boolean;
  /** Chi serve: true se serve NOI. */
  servingNoi: boolean;
  /** 0 = LORO, 1 = NOI, null se la partita non e' finita. */
  winner: 0 | 1 | null;
  /** Valore grezzo del motore: 0, 1, 2, 3, 4 (15, 30, 40, vantaggio). */
  points: [number, number];
  games: [number, number];
  sets: [number, number];
  /** Punti del tie-break, contati uno per uno. */
  tieBreakPoints: [number, number];
}

export interface CommissioningStatusPacket {
  state: CommissioningState;
  authenticated: boolean;
  result: ResultCode;
  /** Secondi che restano alla finestra, 0 se chiusa. */
  remainingSeconds: number;
}

export interface DeviceInfoPacket {
  state: CommissioningState;
  authenticated: boolean;
  firmware: number;
  /** Due byte dell'indirizzo della scheda: sono il suffisso del suo nome. */
  shortId: number;
}

/* -------------------------------------------------------------------------- */
/* Errori                                                                     */
/* -------------------------------------------------------------------------- */

export class ProtocolError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'ProtocolError';
  }
}

/* -------------------------------------------------------------------------- */
/* Lettura                                                                    */
/* -------------------------------------------------------------------------- */

function view(dv: DataView, wanted: number, type: MessageType): void {
  if (dv.byteLength < wanted) {
    throw new ProtocolError(`pacchetto tagliato: ${dv.byteLength} byte invece di ${wanted}`);
  }
  if (dv.getUint8(0) !== PROTOCOL_VERSION) {
    throw new ProtocolError(`versione del protocollo ${dv.getUint8(0)}, attesa ${PROTOCOL_VERSION}`);
  }
  if (dv.getUint8(1) !== type) {
    throw new ProtocolError(`tipo di messaggio ${dv.getUint8(1)}, atteso ${type}`);
  }
}

export function decodeScoreState(dv: DataView): ScoreStatePacket {
  view(dv, SCORE_PACKET_SIZE, MessageType.ScoreState);

  const flags = dv.getUint8(2);
  const winner = dv.getUint8(3);

  return {
    sequence: dv.getUint16(4, true),
    tieBreak: (flags & FLAG_TIE_BREAK) !== 0,
    finished: (flags & FLAG_FINISHED) !== 0,
    servingNoi: (flags & FLAG_SERVING_NOI) !== 0,
    winner: winner === WINNER_NONE ? null : (winner as 0 | 1),
    points: [dv.getUint8(6), dv.getUint8(7)],
    games: [dv.getUint8(8), dv.getUint8(9)],
    sets: [dv.getUint8(10), dv.getUint8(11)],
    tieBreakPoints: [dv.getUint16(12, true), dv.getUint16(14, true)],
  };
}

export function decodeCommissioningStatus(dv: DataView): CommissioningStatusPacket {
  view(dv, STATUS_PACKET_SIZE, MessageType.CommissioningStatus);

  return {
    state: dv.getUint8(2) as CommissioningState,
    authenticated: dv.getUint8(3) !== 0,
    result: dv.getUint8(4) as ResultCode,
    remainingSeconds: dv.getUint8(5),
  };
}

export function decodeDeviceInfo(dv: DataView): DeviceInfoPacket {
  view(dv, DEVICE_INFO_SIZE, MessageType.DeviceInfo);

  return {
    state: dv.getUint8(2) as CommissioningState,
    authenticated: dv.getUint8(3) !== 0,
    firmware: dv.getUint16(4, true),
    shortId: dv.getUint16(6, true),
  };
}

/* -------------------------------------------------------------------------- */
/* Scrittura                                                                  */
/* -------------------------------------------------------------------------- */

/**
 * Un vettore di byte appoggiato a un ``ArrayBuffer`` normale.
 *
 * Web Bluetooth accetta solo buffer che stanno in memoria ordinaria, non quelli
 * condivisi: dichiararlo qui evita di doverlo spiegare al compilatore a ogni
 * chiamata.
 */
export type ByteArray = Uint8Array<ArrayBuffer>;

export function encodeControl(op: ControlOp, token: Uint8Array): ByteArray {
  if (token.length !== TOKEN_LENGTH) {
    throw new ProtocolError(`token di ${token.length} byte invece di ${TOKEN_LENGTH}`);
  }

  const out = new Uint8Array(new ArrayBuffer(CONTROL_PACKET_SIZE));
  out[0] = op;
  out.set(token, 1);
  return out;
}

/* -------------------------------------------------------------------------- */
/* Presentazione dei valori                                                   */
/* -------------------------------------------------------------------------- */

/**
 * Come si scrive un punteggio di game.
 *
 * E' una traduzione da numero a etichetta, non un calcolo: i passaggi da 15 a 30
 * e da 40 a game li fa la scheda, che e' l'unica a sapere come stanno le cose.
 */
const POINT_LABELS = ['0', '15', '30', '40', 'AD'];

export function pointLabel(points: number): string {
  return POINT_LABELS[points] ?? '?';
}

/**
 * Il punteggio da mostrare per una squadra.
 *
 * Nel tie-break i punti veri sono quelli contati uno per uno: i punti del game
 * restano a zero, ed e' la scheda a dirlo alzando il bit del tie-break.
 */
export function scoreLabel(packet: ScoreStatePacket, side: 0 | 1): string {
  return packet.tieBreak
    ? String(packet.tieBreakPoints[side])
    : pointLabel(packet.points[side]);
}
