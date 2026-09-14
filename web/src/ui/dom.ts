/**
 * Piccoli aiuti per costruire la pagina senza framework.
 *
 * Non c'e' nessuna libreria di interfaccia: la pagina e' fatta di poche cose,
 * e scriverle a mano e' piu' chiaro che imparare una sintassi in piu'.
 */

export function el<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  className?: string,
  text?: string,
): HTMLElementTagNameMap[K] {
  const node = document.createElement(tag);
  if (className !== undefined) {
    node.className = className;
  }
  if (text !== undefined) {
    node.textContent = text;
  }
  return node;
}

export function clear(node: HTMLElement): void {
  node.replaceChildren();
}

/** Un elenco di "voce: valore" con un'etichetta grigia e il valore accanto. */
export function facts(rows: Array<[string, string]>): HTMLDListElement {
  const list = document.createElement('dl');
  list.className = 'facts';

  for (const [label, value] of rows) {
    list.append(el('dt', undefined, label), el('dd', undefined, value));
  }

  return list;
}

export interface ButtonOptions {
  /** Il pulsante principale della schermata. */
  primary?: boolean;
  disabled?: boolean;
}

export function button(
  label: string,
  onClick: () => void,
  options: ButtonOptions = {},
): HTMLButtonElement {
  const node = el('button', options.primary === true ? 'primary' : undefined, label);
  node.type = 'button';
  node.disabled = options.disabled === true;
  node.addEventListener('click', onClick);
  return node;
}

/** Una riga di stato: il pallino colorato e il testo. */
export function statusLine(kind: 'ok' | 'warn' | 'off', text: string): HTMLParagraphElement {
  const node = el('p', `status status-${kind}`);
  node.append(el('span', 'dot'), el('span', undefined, text));
  return node;
}
