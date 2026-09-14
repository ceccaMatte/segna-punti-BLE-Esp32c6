/**
 * L'associazione salvata nel browser.
 *
 * Tiene il minimo per non dover rifare il commissioning ogni volta: l'identita'
 * della scheda (il suo `id` per il browser e il nome breve per l'occhio) e il
 * token generato durante l'associazione.
 *
 * Il token e' un segreto, ma non e' una password: sta in `localStorage` perche'
 * questo e' un prototipo, e chi ha accesso al browser ha gia' accesso a tutto.
 * In un prodotto vero andrebbe trattato meglio, e questo e' scritto nelle
 * limitazioni.
 */

import { fromHex, toHex } from '../ble/hex';

const STORAGE_KEY = 'padel.association.v1';

export interface Association {
  /** Identificatore della scheda per questo browser. */
  browserDeviceId: string;
  /** Nome breve della scheda (A31F): serve solo a mostrarlo. */
  shortId: string;
  /** Nome con cui la scheda si annuncia. */
  deviceName: string;
  /** Il token di 16 byte. */
  token: Uint8Array;
  /** Quando e' stata creata, in millisecondi da epoch. */
  savedAt: number;
}

/** Quello che si scrive davvero nella memoria del browser: il token in esadecimale. */
interface StoredAssociation {
  browserDeviceId: string;
  shortId: string;
  deviceName: string;
  tokenHex: string;
  savedAt: number;
}

export class CommissioningStorage {
  private readonly storage: Storage;

  constructor(storage: Storage = localStorage) {
    this.storage = storage;
  }

  load(): Association | null {
    const raw = this.storage.getItem(STORAGE_KEY);
    if (raw === null) {
      return null;
    }

    try {
      const stored = JSON.parse(raw) as StoredAssociation;
      return {
        browserDeviceId: stored.browserDeviceId,
        shortId: stored.shortId,
        deviceName: stored.deviceName,
        token: fromHex(stored.tokenHex),
        savedAt: stored.savedAt,
      };
    } catch {
      /* Memoria illeggibile o scritta da una versione diversa: si riparte da
         zero invece di far cadere la pagina. */
      this.clear();
      return null;
    }
  }

  save(association: Association): void {
    const stored: StoredAssociation = {
      browserDeviceId: association.browserDeviceId,
      shortId: association.shortId,
      deviceName: association.deviceName,
      tokenHex: toHex(association.token),
      savedAt: association.savedAt,
    };
    this.storage.setItem(STORAGE_KEY, JSON.stringify(stored));
  }

  /**
   * Dimentica l'associazione locale.
   *
   * Non tocca la scheda: quella si cancella tenendo basso il piedino di
   * commissioning. Qui si cancella solo quello che sa il browser.
   */
  clear(): void {
    this.storage.removeItem(STORAGE_KEY);
  }
}
