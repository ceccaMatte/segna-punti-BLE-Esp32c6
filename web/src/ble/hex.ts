/**
 * Piccolo aiuto per leggere gli esadecimale: serve al token, che si salva nel
 * browser come stringa e si rilegge come byte.
 */

export function toHex(bytes: Uint8Array): string {
  let out = '';
  for (const byte of bytes) {
    out += byte.toString(16).padStart(2, '0');
  }
  return out;
}

export function fromHex(text: string): Uint8Array {
  if (text.length % 2 !== 0 || /[^0-9a-fA-F]/.test(text)) {
    throw new Error('testo esadecimale non valido');
  }

  const out = new Uint8Array(text.length / 2);
  for (let i = 0; i < out.length; i += 1) {
    out[i] = Number.parseInt(text.slice(i * 2, i * 2 + 2), 16);
  }
  return out;
}

/** Il nome breve della scheda a partire dai due byte che arrivano nel pacchetto. */
export function shortIdFromValue(value: number): string {
  return value.toString(16).toUpperCase().padStart(4, '0');
}
