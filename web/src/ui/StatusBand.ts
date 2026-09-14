/**
 * La banda in alto: la risposta a "sto vedendo la scheda?".
 *
 * Sta sopra tutto il resto e non sparisce mai, nemmeno quando si sta giocando:
 * e' la prima cosa che deve saltare all'occhio, perche' e' l'unica domanda a
 * cui la pagina deve rispondere senza che nessuno la cerchi. Il resto — nome,
 * nome breve, firmware — sta nella riga sotto, per chi vuole sapere *quale*
 * scheda sta guardando.
 *
 * Non decide niente: la riga grossa e la riga sotto arrivano da `status.ts`,
 * che si prova sul computer.
 */

import { clear, el } from './dom';
import { detailLine, headline, type DataFacts, type RetryFacts, type StatusFacts } from './status';

export class StatusBand {
  constructor(private readonly root: HTMLElement) {}

  render(facts: StatusFacts, data: DataFacts, retry: RetryFacts, ident: string | null): void {
    clear(this.root);

    const head = headline(facts, retry);

    /* Il colore della banda e' quello del suo stato: verde quando la scheda
       risponde, giallo quando manca qualcosa o si sta riprovando, grigio
       quando non c'e' nessuno. */
    this.root.className = `band band-${head.kind}`;

    const line = el('p', 'band-head');
    line.append(el('span', 'dot'), el('span', undefined, head.text));
    this.root.append(line, el('p', 'band-detail', detailLine(facts, data, retry)));

    if (ident !== null && ident !== '') {
      this.root.append(el('p', 'band-ident', ident));
    }
  }
}
