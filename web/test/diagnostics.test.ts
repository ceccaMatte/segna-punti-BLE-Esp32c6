/**
 * La diagnostica conta bene quello che deve contare?
 *
 * I tre casi che servono: quando arriva tutto, quando qualcosa si perde e quando
 * qualcosa arriva due volte. Il quarto e' il giro dei sedici bit, che dopo
 * sessantacinquemila pacchetti altrimenti sembrerebbe un salto enorme.
 */

import { beforeEach, describe, expect, it } from 'vitest';

import { PacketDiagnostics } from '../src/ble/diagnostics';

let diagnostics: PacketDiagnostics;

beforeEach(() => {
  diagnostics = new PacketDiagnostics();
});

describe('conteggio dei pacchetti', () => {
  it('il primo pacchetto non conta come salto ne\' come duplicato', () => {
    diagnostics.notePacket(5, 1000);

    expect(diagnostics.packets).toBe(1);
    expect(diagnostics.lastSequence).toBe(5);
    expect(diagnostics.gaps).toBe(0);
    expect(diagnostics.duplicates).toBe(0);
  });

  it('una sequenza di numeri consecutivi non produce salti', () => {
    for (let sequence = 1; sequence <= 10; sequence += 1) {
      diagnostics.notePacket(sequence, 1000 + sequence);
    }

    expect(diagnostics.packets).toBe(10);
    expect(diagnostics.gaps).toBe(0);
    expect(diagnostics.duplicates).toBe(0);
    expect(diagnostics.lastSequence).toBe(10);
  });

  it('conta i buchi quando un numero non arriva', () => {
    diagnostics.notePacket(1, 1);
    diagnostics.notePacket(2, 2);
    diagnostics.notePacket(5, 3);

    expect(diagnostics.gaps).toBe(2);
    expect(diagnostics.lastSequence).toBe(5);
    expect(diagnostics.packets).toBe(3);
  });

  it('conta i duplicati', () => {
    diagnostics.notePacket(1, 1);
    diagnostics.notePacket(3, 2);
    diagnostics.notePacket(3, 3);
    diagnostics.notePacket(4, 4);

    expect(diagnostics.duplicates).toBe(1);
    expect(diagnostics.gaps).toBe(1);
  });

  it('sa contare anche nel giro dei sedici bit', () => {
    diagnostics.notePacket(65534, 1);
    diagnostics.notePacket(65535, 2);
    diagnostics.notePacket(0, 3);
    diagnostics.notePacket(1, 4);

    expect(diagnostics.gaps).toBe(0);
    expect(diagnostics.duplicates).toBe(0);
    expect(diagnostics.lastSequence).toBe(1);
  });
});

describe('tempi ed errori', () => {
  it('misura da quanto non arriva niente', () => {
    expect(diagnostics.ageMs(5000)).toBeNull();

    diagnostics.notePacket(1, 4000);
    expect(diagnostics.ageMs(5000)).toBe(1000);
  });

  it('conta le disconnessioni e ricorda l\'ultimo errore', () => {
    diagnostics.noteDisconnect();
    diagnostics.noteDisconnect();
    diagnostics.noteError('collegamento caduto');

    expect(diagnostics.disconnects).toBe(2);
    expect(diagnostics.lastError).toBe('collegamento caduto');
  });

  it('azzerando si dimentica tutto tranne le disconnessioni', () => {
    diagnostics.notePacket(10, 1);
    diagnostics.noteDisconnect();
    diagnostics.reset();

    expect(diagnostics.packets).toBe(0);
    expect(diagnostics.lastSequence).toBeNull();
    expect(diagnostics.lastError).toBeNull();
    /* Le disconnessioni restano: raccontano la sessione, non il collegamento. */
    expect(diagnostics.disconnects).toBe(1);
  });
});
