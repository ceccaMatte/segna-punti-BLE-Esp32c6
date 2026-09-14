/**
 * Il conto del silenzio regge?
 *
 * E' la parte da cui dipende quanto ci si mette ad accorgersi che la scheda non
 * c'e' piu', e ha due modi di sbagliare opposti, tutti e due fastidiosi: non
 * accorgersene mai, oppure chiudere un collegamento che stava benissimo perche'
 * un battito si e' perso per strada. Le prove guardano tutti e due.
 */

import { describe, expect, it } from 'vitest';

import { HEARTBEAT_MS, SILENCE_TIMEOUT_MS, SilenceWatch } from '../src/ble/liveness';

describe('le due costanti stanno insieme', () => {
  it('si aspetta piu\' di un battito, ma non tre', () => {
    expect(SILENCE_TIMEOUT_MS).toBeGreaterThan(HEARTBEAT_MS);
    expect(SILENCE_TIMEOUT_MS).toBeLessThan(3 * HEARTBEAT_MS);
  });
});

describe('il conto del silenzio', () => {
  it('senza aver mai sentito niente non scatta', () => {
    const watch = new SilenceWatch();

    /* Prima del primo battito il silenzio non vuol dire niente: la pagina sta
       ancora aspettando di sapere qualcosa. */
    expect(watch.expired(1_000_000)).toBe(false);
    expect(watch.silenceMs(1_000_000)).toBeNull();
  });

  it('scatta dopo due battiti mancati', () => {
    const watch = new SilenceWatch();
    watch.noteAlive(1000);

    expect(watch.expired(1000 + SILENCE_TIMEOUT_MS)).toBe(false);
    expect(watch.expired(1001 + SILENCE_TIMEOUT_MS)).toBe(true);
  });

  it('un battito solo non basta a farla scattare', () => {
    const watch = new SilenceWatch();
    watch.noteAlive(1000);

    /* Un pacchetto perso per disturbo non deve far chiudere niente. */
    expect(watch.expired(1000 + HEARTBEAT_MS)).toBe(false);
    expect(watch.expired(1000 + 2 * HEARTBEAT_MS)).toBe(false);
  });

  it('un battito rimette il conto a zero', () => {
    const watch = new SilenceWatch();
    watch.noteAlive(1000);
    watch.noteAlive(4000);

    expect(watch.expired(4000 + SILENCE_TIMEOUT_MS)).toBe(false);
    expect(watch.silenceMs(4100)).toBe(100);
  });

  it('misura da quanto tace', () => {
    const watch = new SilenceWatch();
    watch.noteAlive(1000);

    expect(watch.silenceMs(1900)).toBe(900);
  });

  it('dopo un azzeramento si torna a non sapere niente', () => {
    const watch = new SilenceWatch();
    watch.noteAlive(1000);

    /* E' quello che si fa quando il collegamento e' stato chiuso a mano: il
       conto riparte dalla connessione successiva. */
    watch.reset();

    expect(watch.expired(999_999)).toBe(false);
    expect(watch.silenceMs(999_999)).toBeNull();
  });
});
