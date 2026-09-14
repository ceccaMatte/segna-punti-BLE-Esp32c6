/**
 * La riconnessione automatica fa quello che promette?
 *
 * Le cose che vanno verificate sono poche e tutte importanti: che il primo
 * tentativo arrivi subito e i successivi si diradino, che non partano due
 * tentativi insieme (si darebbero fastidio a vicenda), che un successo fermi
 * tutto, e che dopo uno stop non arrivi piu' niente — nemmeno un collegamento
 * che era gia' in programma.
 */

import { afterEach, describe, expect, it, vi } from 'vitest';

import { AutoReconnect, RETRY_DELAYS_MS, retryDelayMs } from '../src/ble/reconnect';

afterEach(() => {
  vi.useRealTimers();
});

describe('quanto si aspetta fra un tentativo e l\'altro', () => {
  it('il primo tentativo e\' il piu\' vicino', () => {
    expect(retryDelayMs(0)).toBe(RETRY_DELAYS_MS[0]);
    expect(retryDelayMs(0)).toBeLessThanOrEqual(retryDelayMs(8));
  });

  it('i tempi si allargano ma non all\'infinito', () => {
    expect(retryDelayMs(1)).toBe(500);
    expect(retryDelayMs(2)).toBe(1000);
    expect(retryDelayMs(4)).toBe(2000);
    expect(retryDelayMs(6)).toBe(3000);
    expect(retryDelayMs(7)).toBe(5000);
    expect(retryDelayMs(8)).toBe(8000);
    expect(retryDelayMs(50)).toBe(8000);
  });

  it('l\'inizio e\' fitto: la scheda che si riavvia torna in due secondi', () => {
    /* Cinque tentativi entro i primi due secondi: e' la finestra in cui la
       scheda si riavvia, e va coperta tentando spesso, non aspettando. */
    expect(retryDelayMs(0) + retryDelayMs(1) + retryDelayMs(2) + retryDelayMs(3)).toBeLessThanOrEqual(
      3000,
    );
  });

  it('un conto senza senso non fa saltare niente', () => {
    expect(retryDelayMs(-3)).toBe(RETRY_DELAYS_MS[0]);
  });
});

describe('il giro dei tentativi', () => {
  it('il primo tentativo arriva subito, poi i tempi si allargano', async () => {
    vi.useFakeTimers();

    let attempts = 0;
    const reconnector = new AutoReconnect({
      attempt: async () => {
        attempts += 1;
        return false;
      },
      onChange: () => {},
    });

    reconnector.start(true);
    expect(attempts).toBe(1);

    await vi.advanceTimersByTimeAsync(499);
    expect(attempts).toBe(1);

    await vi.advanceTimersByTimeAsync(1);
    expect(attempts).toBe(2);

    await vi.advanceTimersByTimeAsync(500);
    expect(attempts).toBe(3);

    await vi.advanceTimersByTimeAsync(2000);
    expect(attempts).toBe(5);

    await vi.advanceTimersByTimeAsync(4000);
    expect(attempts).toBe(7);

    await vi.advanceTimersByTimeAsync(3000);
    expect(attempts).toBe(8);

    await vi.advanceTimersByTimeAsync(5000);
    expect(attempts).toBe(9);

    /* Da qui in avanti sempre allo stesso passo, per quanto si aspetti. */
    await vi.advanceTimersByTimeAsync(8000 * 3);
    expect(attempts).toBe(12);
  });

  it('dopo una caduta si aspetta un momento prima di riprovare', async () => {
    vi.useFakeTimers();

    let attempts = 0;
    const reconnector = new AutoReconnect({
      attempt: async () => {
        attempts += 1;
        return false;
      },
      onChange: () => {},
    });

    /* La radio ha appena perso il collegamento: un tentativo immediato
       fallirebbe di sicuro. */
    reconnector.start(false);
    expect(attempts).toBe(0);

    await vi.advanceTimersByTimeAsync(499);
    expect(attempts).toBe(0);

    await vi.advanceTimersByTimeAsync(1);
    expect(attempts).toBe(1);
  });

  it('appena riesce si smette di riprovare', async () => {
    vi.useFakeTimers();

    let attempts = 0;
    const reconnector = new AutoReconnect({
      attempt: async () => {
        attempts += 1;
        return attempts >= 3;
      },
      onChange: () => {},
    });

    reconnector.start(false);
    await vi.advanceTimersByTimeAsync(500);
    await vi.advanceTimersByTimeAsync(500);
    await vi.advanceTimersByTimeAsync(1000);
    expect(attempts).toBe(3);

    await vi.advanceTimersByTimeAsync(120000);
    expect(attempts).toBe(3);
    expect(reconnector.view().attempts).toBe(0);
    expect(reconnector.view().nextAttemptAtMs).toBeNull();
  });

  it('non partono due tentativi insieme', async () => {
    vi.useFakeTimers();

    let attempts = 0;
    const pending: Array<(ok: boolean) => void> = [];

    const reconnector = new AutoReconnect({
      attempt: () => {
        attempts += 1;
        return new Promise<boolean>((resolve) => {
          pending.push(resolve);
        });
      },
      onChange: () => {},
    });

    reconnector.start(true);
    expect(attempts).toBe(1);
    expect(reconnector.view().attempting).toBe(true);

    /* Il computer si e' risvegliato e la pagina lo chiama: ma un tentativo e'
       gia' in corso, e due connect() sulla stessa scheda si danno fastidio. */
    reconnector.retryNow();
    expect(attempts).toBe(1);

    pending.shift()?.(false);
    await vi.advanceTimersByTimeAsync(0);
    expect(reconnector.view().attempting).toBe(false);
  });

  it('dopo lo stop non arriva piu\' niente', async () => {
    vi.useFakeTimers();

    let attempts = 0;
    const reconnector = new AutoReconnect({
      attempt: async () => {
        attempts += 1;
        return false;
      },
      onChange: () => {},
    });

    reconnector.start(false);
    reconnector.stop();

    await vi.advanceTimersByTimeAsync(600000);

    expect(attempts).toBe(0);
    expect(reconnector.view().enabled).toBe(false);
  });

  it('si puo\' ricominciare dopo uno stop', async () => {
    vi.useFakeTimers();

    let attempts = 0;
    const reconnector = new AutoReconnect({
      attempt: async () => {
        attempts += 1;
        return false;
      },
      onChange: () => {},
    });

    reconnector.stop();
    reconnector.start(true);

    expect(attempts).toBe(1);
    expect(reconnector.view().enabled).toBe(true);
  });

  it('chiamare start mentre sta gia\' lavorando non fa ripartire il conto', async () => {
    vi.useFakeTimers();

    let attempts = 0;
    const reconnector = new AutoReconnect({
      attempt: async () => {
        attempts += 1;
        return false;
      },
      onChange: () => {},
    });

    reconnector.start(false);
    await vi.advanceTimersByTimeAsync(500);
    await vi.advanceTimersByTimeAsync(500);
    await vi.advanceTimersByTimeAsync(1000);
    expect(attempts).toBe(3);

    reconnector.start(false);
    expect(reconnector.view().attempts).toBe(3);
    expect(attempts).toBe(3);
  });

  it('un errore del tentativo vale come un tentativo fallito', async () => {
    vi.useFakeTimers();

    let attempts = 0;
    const reconnector = new AutoReconnect({
      attempt: async () => {
        attempts += 1;
        throw new Error('scheda spenta');
      },
      onChange: () => {},
    });

    reconnector.start(true);
    expect(attempts).toBe(1);

    await vi.advanceTimersByTimeAsync(500);
    expect(attempts).toBe(2);
  });
});
