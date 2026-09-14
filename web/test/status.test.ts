/**
 * La pagina dice chiaramente se sta vedendo la scheda?
 *
 * E' l'unica domanda a cui la pagina deve rispondere senza che nessuno la
 * cerchi, quindi vale la pena di provare tutte le risposte possibili: collegata
 * e riconosciuta, collegata ma non riconosciuta, non collegata con una scheda
 * in memoria, non collegata senza niente in memoria, browser senza Web
 * Bluetooth, richiesta in corso.
 */

import { describe, expect, it } from 'vitest';

import { detailLine, formatAge, headline, type StatusFacts } from '../src/ui/status';

/** Una situazione di partenza: non collegata, niente in memoria. */
function facts(overrides: Partial<StatusFacts> = {}): StatusFacts {
  return {
    supported: true,
    connected: false,
    authenticated: false,
    connectedName: null,
    knownName: null,
    busy: false,
    ...overrides,
  };
}

describe('la riga grossa', () => {
  it('senza Web Bluetooth lo dice e basta', () => {
    const result = headline(facts({ supported: false }));

    expect(result.kind).toBe('off');
    expect(result.text).toContain('Web Bluetooth');
  });

  it('mentre si sta collegando non dice ne\' collegata ne\' scollegata', () => {
    const result = headline(facts({ busy: true }));

    expect(result.kind).toBe('warn');
    expect(result.text).toContain('Collegamento in corso');
  });

  it('non collegata, senza niente in memoria', () => {
    const result = headline(facts());

    expect(result.kind).toBe('off');
    expect(result.text).toBe('NESSUNA SCHEDA COLLEGATA');
  });

  it('non collegata, ma ricorda quale scheda era', () => {
    const result = headline(facts({ knownName: 'PADEL_SCORE_EE26' }));

    expect(result.kind).toBe('off');
    expect(result.text).toContain('PADEL_SCORE_EE26');
    expect(result.text).toContain('NON COLLEGATA');
  });

  it('collegata ma non riconosciuta: e\' un avviso, non un errore', () => {
    const result = headline(facts({ connected: true, connectedName: 'PADEL_SCORE_EE26' }));

    expect(result.kind).toBe('warn');
    expect(result.text).toContain('PADEL_SCORE_EE26');
    expect(result.text).toContain('non ancora riconosciuta');
  });

  it('collegata e riconosciuta: e\' l\'unico caso buono', () => {
    const result = headline(
      facts({ connected: true, authenticated: true, connectedName: 'PADEL_SCORE_EE26' }),
    );

    expect(result.kind).toBe('ok');
    expect(result.text).toBe('COLLEGATA a PADEL_SCORE_EE26');
  });
});

describe('la riga sotto', () => {
  it('senza Web Bluetooth spiega cosa serve', () => {
    expect(detailLine(facts({ supported: false }), { ageMs: null, remainingSeconds: null })).toContain(
      'Chrome',
    );
  });

  it('con la finestra aperta mette davanti il conto alla rovescia', () => {
    const text = detailLine(facts({ connected: true, authenticated: true }), {
      ageMs: 100,
      remainingSeconds: 42,
    });

    expect(text).toContain('42 s');
    expect(text).toContain('Commissioning');
  });

  it('non collegata: dice quale pulsante premere', () => {
    const conMemoria = detailLine(facts({ knownName: 'PADEL_SCORE_EE26' }), {
      ageMs: null,
      remainingSeconds: null,
    });
    const senzaMemoria = detailLine(facts(), { ageMs: null, remainingSeconds: null });

    expect(conMemoria).toContain('RICONNETTI');
    expect(senzaMemoria).toContain('COMMISSIONA SCHEDA');
  });

  it('collegata ma non riconosciuta: avverte che il punteggio non arriva', () => {
    const text = detailLine(facts({ connected: true, connectedName: 'PADEL_SCORE_EE26' }), {
      ageMs: null,
      remainingSeconds: null,
    });

    expect(text).toContain('non arriva');
  });

  it('riconosciuta: dice da quanto non arriva niente', () => {
    const collegata = facts({ connected: true, authenticated: true });

    expect(detailLine(collegata, { ageMs: null, remainingSeconds: null })).toContain('primo');
    expect(detailLine(collegata, { ageMs: 400, remainingSeconds: null })).toContain('400 ms');
    expect(detailLine(collegata, { ageMs: 2500, remainingSeconds: null })).toContain('2.5 s');
  });
});

describe('le durate', () => {
  it('sotto il secondo si contano i millisecondi', () => {
    expect(formatAge(0)).toBe('0 ms');
    expect(formatAge(999)).toBe('999 ms');
  });

  it('sopra il secondo i decimi bastano', () => {
    expect(formatAge(1000)).toBe('1.0 s');
    expect(formatAge(1543)).toBe('1.5 s');
    expect(formatAge(59000)).toBe('59.0 s');
  });

  it('sopra il minuto si cambia unita\'', () => {
    expect(formatAge(60000)).toBe('1 min');
    expect(formatAge(185000)).toBe('3 min');
    expect(formatAge(3600000)).toBe('1 h');
  });

  it('un tempo negativo, che vorrebbe dire orologio storto, non si mostra', () => {
    expect(formatAge(-500)).toBe('0 ms');
  });
});
