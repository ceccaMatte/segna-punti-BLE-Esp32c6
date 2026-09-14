/**
 * L'associazione salvata nel browser.
 *
 * Si prova con una memoria finta: non serve un browser per sapere se il token
 * si scrive, si rilegge uguale e sparisce quando lo si dimentica.
 */

import { beforeEach, describe, expect, it } from 'vitest';

import { CommissioningStorage, type Association } from '../src/storage/CommissioningStorage';

class MemoryStorage implements Storage {
  private readonly map = new Map<string, string>();

  get length(): number {
    return this.map.size;
  }

  clear(): void {
    this.map.clear();
  }

  getItem(key: string): string | null {
    return this.map.get(key) ?? null;
  }

  key(index: number): string | null {
    return Array.from(this.map.keys())[index] ?? null;
  }

  removeItem(key: string): void {
    this.map.delete(key);
  }

  setItem(key: string, value: string): void {
    this.map.set(key, value);
  }
}

function sample(): Association {
  return {
    browserDeviceId: 'browser-device-id',
    shortId: 'A31F',
    deviceName: 'PADEL_SCORE_A31F',
    token: new Uint8Array(16).map((_, index) => index * 7 + 1),
    savedAt: 1_700_000_000_000,
  };
}

let storage: MemoryStorage;
let subject: CommissioningStorage;

beforeEach(() => {
  storage = new MemoryStorage();
  subject = new CommissioningStorage(storage);
});

describe('associazione locale', () => {
  it('senza niente salvato non restituisce niente', () => {
    expect(subject.load()).toBeNull();
  });

  it('quello che si salva si rilegge uguale', () => {
    const original = sample();
    subject.save(original);

    const read = subject.load();

    expect(read).not.toBeNull();
    expect(read?.browserDeviceId).toBe(original.browserDeviceId);
    expect(read?.shortId).toBe(original.shortId);
    expect(read?.deviceName).toBe(original.deviceName);
    expect(Array.from(read?.token ?? [])).toEqual(Array.from(original.token));
    expect(read?.savedAt).toBe(original.savedAt);
  });

  it('il token si salva come testo esadecimale', () => {
    subject.save(sample());

    const raw = storage.getItem('padel.association.v1') ?? '';
    expect(raw).toContain('"tokenHex"');
    expect(raw).not.toContain('"token"');
  });

  it('dimenticare cancella tutto', () => {
    subject.save(sample());
    subject.clear();

    expect(subject.load()).toBeNull();
  });

  it('una memoria rovinata non fa cadere la pagina', () => {
    storage.setItem('padel.association.v1', '{ questo non e\' JSON');

    expect(subject.load()).toBeNull();
    /* E si e' anche ripulito, cosi' il prossimo giro parte pulito. */
    expect(storage.getItem('padel.association.v1')).toBeNull();
  });

  it('un token malformato viene rifiutato', () => {
    storage.setItem(
      'padel.association.v1',
      JSON.stringify({
        browserDeviceId: 'x',
        shortId: 'A31F',
        deviceName: 'PADEL_SCORE_A31F',
        tokenHex: 'non-esadecimale',
        savedAt: 0,
      }),
    );

    expect(subject.load()).toBeNull();
  });
});
