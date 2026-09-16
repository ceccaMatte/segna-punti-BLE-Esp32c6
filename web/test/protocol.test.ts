/**
 * Il protocollo, provato con gli stessi valori di esempio del firmware.
 *
 * Il caso che conta di piu' e' quello che costruisce i byte a mano e li legge:
 * se qualcuno cambia la disposizione dei campi da una parte sola, qui si vede.
 */

import { describe, expect, it } from 'vitest';

import {
  CommissioningState,
  ControlOp,
  FLAG_FINISHED,
  FLAG_HEARTBEAT,
  FLAG_SERVING_NOI,
  FLAG_TIE_BREAK,
  PadelEvent,
  PROTOCOL_VERSION,
  ProtocolError,
  ResultCode,
  WINNER_NONE,
  decodeCommissioningStatus,
  decodeDeviceInfo,
  decodeScoreState,
  encodeControl,
  eventName,
  pointLabel,
  scoreLabel,
} from '../src/ble/protocol';

function bytes(values: number[]): DataView {
  return new DataView(new Uint8Array(values).buffer);
}

describe('stato della partita', () => {
  it('legge i campi dove sono stati scritti', () => {
    const packet = decodeScoreState(
      bytes([
        PROTOCOL_VERSION,
        1, // SCORE_STATE
        FLAG_TIE_BREAK | FLAG_SERVING_NOI,
        WINNER_NONE,
        0x34,
        0x12, // sequenza 0x1234
        3,
        4, // punti LORO, NOI
        5,
        6, // game
        1,
        2, // set
        6,
        0, // tie-break LORO
        7,
        0, // tie-break NOI
        PadelEvent.OurPoint, // in coda: perche' e' partito
      ]),
    );

    expect(packet.sequence).toBe(0x1234);
    expect(packet.event).toBe(PadelEvent.OurPoint);
    expect(packet.heartbeat).toBe(false);
    expect(packet.tieBreak).toBe(true);
    expect(packet.servingNoi).toBe(true);
    expect(packet.finished).toBe(false);
    expect(packet.winner).toBeNull();
    expect(packet.points).toEqual([3, 4]);
    expect(packet.games).toEqual([5, 6]);
    expect(packet.sets).toEqual([1, 2]);
    expect(packet.tieBreakPoints).toEqual([6, 7]);
  });

  it('riconosce il battito: stesso stato, nessuna novita\'', () => {
    const packet = decodeScoreState(
      bytes([
        PROTOCOL_VERSION,
        1, // SCORE_STATE
        FLAG_HEARTBEAT | FLAG_SERVING_NOI,
        WINNER_NONE,
        7,
        0, // sequenza: quella di prima, non una nuova
        0,
        1, // punti
        0,
        0, // game
        0,
        0, // set
        0,
        0,
        0,
        0, // tie-break
        PadelEvent.None, // un battito non annuncia nessun gesto
      ]),
    );

    expect(packet.heartbeat).toBe(true);
    expect(packet.event).toBe(PadelEvent.None);
    expect(packet.sequence).toBe(7);
    expect(packet.servingNoi).toBe(true);
    expect(packet.points).toEqual([0, 1]);
  });

  it('riconosce la partita finita con il suo vincitore', () => {
    const packet = decodeScoreState(
      bytes([
        PROTOCOL_VERSION,
        1,
        FLAG_FINISHED,
        1,
        9,
        0,
        0,
        0,
        0,
        0,
        3,
        2,
        0,
        0,
        0,
        0,
        PadelEvent.StateSync,
      ]),
    );

    expect(packet.finished).toBe(true);
    expect(packet.winner).toBe(1);
    expect(packet.sets).toEqual([3, 2]);
  });

  it('senza il byte dell\'evento il pacchetto si rifiuta', () => {
    /* Sedici byte sono il pacchetto di prima: adesso sono tagliati, e
       l'evento non si puo' inventare. */
    const old = [PROTOCOL_VERSION, 1, 0, WINNER_NONE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];

    expect(() => decodeScoreState(bytes(old))).toThrow(ProtocolError);
  });

  it('rifiuta versione, tipo e lunghezza sbagliati', () => {
    const good = [
      PROTOCOL_VERSION,
      1,
      0,
      WINNER_NONE,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
      PadelEvent.None,
    ];

    expect(() => decodeScoreState(bytes([PROTOCOL_VERSION + 1, ...good.slice(1)]))).toThrow(
      ProtocolError,
    );
    expect(() => decodeScoreState(bytes([PROTOCOL_VERSION, 2, ...good.slice(2)]))).toThrow(
      ProtocolError,
    );
    expect(() => decodeScoreState(bytes(good.slice(0, 10)))).toThrow(ProtocolError);
  });
});

describe('gli eventi', () => {
  it('hanno i numeri e i nomi del firmware', () => {
    /* Gli stessi valori di main/link/ble_protocol.h: se uno dei due lati
       cambia, si vede qui invece che a scheda accesa. */
    expect(PadelEvent.None).toBe(0);
    expect(PadelEvent.OurPoint).toBe(1);
    expect(PadelEvent.TheirPoint).toBe(2);
    expect(PadelEvent.Undo).toBe(3);
    expect(PadelEvent.Moment).toBe(4);
    expect(PadelEvent.StartPairing).toBe(5);
    expect(PadelEvent.Reset).toBe(6);
    expect(PadelEvent.StateSync).toBe(7);

    expect(eventName(PadelEvent.OurPoint)).toBe('OUR_POINT');
    expect(eventName(PadelEvent.Moment)).toBe('MOMENT');
    expect(eventName(PadelEvent.StartPairing)).toBe('START_PAIRING');
    expect(eventName(PadelEvent.StateSync)).toBe('STATE_SYNC');
  });
});

describe('stato dell\'associazione', () => {
  it('legge i campi', () => {
    const status = decodeCommissioningStatus(
      bytes([PROTOCOL_VERSION, 2, CommissioningState.WindowOpen, 0, ResultCode.Timeout, 47]),
    );

    expect(status.state).toBe(CommissioningState.WindowOpen);
    expect(status.authenticated).toBe(false);
    expect(status.result).toBe(ResultCode.Timeout);
    expect(status.remainingSeconds).toBe(47);
  });

  it('rifiuta un pacchetto tagliato', () => {
    expect(() =>
      decodeCommissioningStatus(bytes([PROTOCOL_VERSION, 2, 0, 0, 0])),
    ).toThrow(ProtocolError);
  });
});

describe('informazioni sulla scheda', () => {
  it('legge il nome breve e la versione del firmware', () => {
    const info = decodeDeviceInfo(
      bytes([PROTOCOL_VERSION, 3, CommissioningState.Commissioned, 1, 0x00, 0x01, 0x1f, 0xa3]),
    );

    expect(info.state).toBe(CommissioningState.Commissioned);
    expect(info.authenticated).toBe(true);
    expect(info.firmware).toBe(0x0100);
    expect(info.shortId).toBe(0xa31f);
  });
});

describe('comandi', () => {
  it('scrive opcode e token', () => {
    const token = new Uint8Array(16).map((_, index) => index + 1);
    const payload = encodeControl(ControlOp.Claim, token);

    expect(payload.length).toBe(17);
    expect(payload[0]).toBe(ControlOp.Claim);
    expect(Array.from(payload.slice(1))).toEqual(Array.from(token));
  });

  it('rifiuta un token della lunghezza sbagliata', () => {
    expect(() => encodeControl(ControlOp.Auth, new Uint8Array(15))).toThrow(ProtocolError);
  });
});

describe('come si scrive il punteggio', () => {
  it('traduce i valori del motore in etichette', () => {
    expect(pointLabel(0)).toBe('0');
    expect(pointLabel(1)).toBe('15');
    expect(pointLabel(2)).toBe('30');
    expect(pointLabel(3)).toBe('40');
    expect(pointLabel(4)).toBe('AD');
  });

  it('nel tie-break usa i punti contati uno per uno', () => {
    const packet = decodeScoreState(
      bytes([
        PROTOCOL_VERSION,
        1,
        FLAG_TIE_BREAK,
        WINNER_NONE,
        1,
        0,
        0,
        0,
        6,
        6,
        1,
        1,
        8,
        0,
        7,
        0,
        PadelEvent.None,
      ]),
    );

    /* I punti del game sono a zero, e non devono comparire. */
    expect(scoreLabel(packet, 0)).toBe('8');
    expect(scoreLabel(packet, 1)).toBe('7');
  });

  it('fuori dal tie-break usa i punti del game', () => {
    const packet = decodeScoreState(
      bytes([
        PROTOCOL_VERSION,
        1,
        0,
        WINNER_NONE,
        1,
        0,
        3,
        4,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        PadelEvent.None,
      ]),
    );

    expect(scoreLabel(packet, 0)).toBe('40');
    expect(scoreLabel(packet, 1)).toBe('AD');
  });
});
