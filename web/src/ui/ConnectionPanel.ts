/**
 * Pannello del collegamento e del commissioning.
 *
 * Mostra a che punto siamo e mette a disposizione i pulsanti che servono. Non
 * prende decisioni: lo stato gli arriva gia' pronto da fuori, e i pulsanti si
 * limitano a chiamare chi sa cosa fare.
 *
 * La riga di stato e' la stessa della banda in alto, presa da `status.ts`: due
 * posti che dicono la stessa cosa con parole proprie finiscono prima o poi per
 * contraddirsi. Sotto ci sono i fatti, e in fondo l'elenco delle schede che
 * questa pagina ha il permesso di rivedere, che e' dove si capisce *quale*
 * scheda si sta guardando.
 */

import { CommissioningState } from '../ble/protocol';
import { button, clear, el, facts, statusLine } from './dom';
import { headline, type RetryFacts, type StatusFacts } from './status';

/** Una scheda che questa pagina ha il permesso di rivedere. */
export interface DeviceRow {
  name: string;
  id: string;
  /** Vero se e' la scheda associata a questa pagina. */
  associated: boolean;
  /** Vero se e' quella collegata adesso. */
  connected: boolean;
}

export interface ConnectionView {
  supported: boolean;
  connected: boolean;
  authenticated: boolean;
  /** Nome della scheda collegata adesso, se c'e'. */
  connectedName: string | null;
  /** Nome della scheda che il browser ricorda, anche se non e' collegata. */
  knownName: string | null;
  shortId: string | null;
  /** Versione del firmware dichiarata dalla scheda, se si e' letta. */
  firmware: number | null;
  state: CommissioningState | null;
  remainingSeconds: number | null;
  /** Riga di spiegazione sotto lo stato. */
  message: string | null;
  /** Vero se il browser ricorda un'associazione. */
  hasAssociation: boolean;
  /** Vero mentre una richiesta e' in corso: i pulsanti si spengono. */
  busy: boolean;
  /** Le schede che il browser lascia rivedere a questa pagina. */
  devices: DeviceRow[];
  /** Falso se il browser non sa elencarle (`getDevices()` assente). */
  canListDevices: boolean;
  /** Cosa sta facendo la riconnessione automatica. */
  retry: RetryFacts;
  /** Falso quando la pagina non ha in mano la scheda e non puo' riprenderla da sola. */
  canAutoReconnect: boolean;
}

export interface ConnectionActions {
  onCommission: () => void;
  onReconnect: () => void;
  onDisconnect: () => void;
  onForget: () => void;
  onStopRetry: () => void;
}

/** Lo stato del collegamento, come lo vede `status.ts`. */
function statusFacts(view: ConnectionView): StatusFacts {
  return {
    supported: view.supported,
    connected: view.connected,
    authenticated: view.authenticated,
    connectedName: view.connectedName,
    knownName: view.knownName,
    busy: view.busy,
  };
}

/** Come si chiama, in una riga, la riconnessione automatica. */
function retryText(view: ConnectionView): string {
  if (!view.hasAssociation) {
    /* Senza associazione non c'e' niente da riprendere: la riga non serve. */
    return '—';
  }
  if (!view.canAutoReconnect) {
    return 'non disponibile: serve un click';
  }
  if (view.retry.attempting) {
    return `tentativo in corso (${view.retry.attempts})`;
  }
  if (view.retry.retrying) {
    return `in attesa (${view.retry.attempts} tentativi fatti)`;
  }
  return 'pronta';
}

/** L'elenco delle schede note al browser. */
function deviceList(rows: DeviceRow[]): HTMLUListElement {
  const list = el('ul', 'devices');

  for (const row of rows) {
    const item = el('li', 'device');
    const head = el('div', 'device-head');
    head.append(el('span', 'device-name', row.name));

    if (row.connected) {
      head.append(el('span', 'tag tag-on', 'collegata adesso'));
    } else if (row.associated) {
      head.append(el('span', 'tag', 'associata a questa pagina'));
    }

    item.append(head, el('div', 'device-id', row.id));
    list.append(item);
  }

  return list;
}

export class ConnectionPanel {
  private readonly actions: ConnectionActions;

  constructor(
    private readonly statusRoot: HTMLElement,
    private readonly factsRoot: HTMLElement,
    private readonly messageRoot: HTMLElement,
    private readonly buttonsRoot: HTMLElement,
    private readonly devicesRoot: HTMLElement,
    actions: ConnectionActions,
  ) {
    this.actions = actions;
  }

  render(view: ConnectionView): void {
    clear(this.statusRoot);
    clear(this.factsRoot);
    clear(this.messageRoot);
    clear(this.buttonsRoot);
    clear(this.devicesRoot);

    if (!view.supported) {
      this.statusRoot.append(
        statusLine('off', 'Questo browser non ha Web Bluetooth: servono Chrome o Edge su computer.'),
      );
      return;
    }

    const head = headline(statusFacts(view), view.retry);
    this.statusRoot.append(statusLine(head.kind, head.text));

    const rows: Array<[string, string]> = [
      ['Scheda', view.connectedName ?? view.knownName ?? '—'],
      ['Nome breve', view.shortId ?? '—'],
      ['Firmware', view.firmware !== null ? String(view.firmware) : '—'],
      ['Associazione',
        view.state === CommissioningState.Commissioned ? 'presente' : 'assente'],
      ['Riconnessione automatica', retryText(view)],
    ];

    if (view.remainingSeconds !== null && view.remainingSeconds > 0) {
      rows.push(['Finestra aperta', `${view.remainingSeconds} s`]);
    }

    this.factsRoot.append(facts(rows));

    if (view.message !== null && view.message !== '') {
      this.messageRoot.append(el('p', 'message', view.message));
    }

    this.renderDevices(view);

    /* I pulsanti compaiono solo quando servono: e' piu' facile capire cosa si
       puo' fare guardando cosa c'e' scritto. */
    if (view.busy) {
      this.buttonsRoot.append(button('Attendere…', () => {}, { disabled: true }));
      return;
    }

    if (!view.connected) {
      if (view.retry.retrying) {
        /* Mentre riprova da sola, l'unica cosa che si puo' volere e' che
           smetta. */
        this.buttonsRoot.append(
          button('ANNULLA RICONNESSIONE', () => this.actions.onStopRetry()),
          button('SCEGLI SCHEDA', () => this.actions.onCommission()),
        );
        return;
      }

      if (view.hasAssociation) {
        this.buttonsRoot.append(
          button('RICONNETTI', () => this.actions.onReconnect()),
          button('COMMISSIONA SCHEDA', () => this.actions.onCommission()),
        );
      } else {
        this.buttonsRoot.append(
          button('COMMISSIONA SCHEDA', () => this.actions.onCommission(), { primary: true }),
        );
      }
      return;
    }

    this.buttonsRoot.append(button('SCOLLEGA', () => this.actions.onDisconnect()));

    if (view.authenticated) {
      this.buttonsRoot.append(button('DIMENTICA ASSOCIAZIONE LOCALE', () => this.actions.onForget()));
    }
  }

  /**
   * L'elenco delle schede note al browser.
   *
   * Web Bluetooth non lascia cercare le schede in giro: si vedono solo quelle
   * che qualcuno ha gia' scelto almeno una volta da questa pagina. Non e' un
   * limite da aggirare, ma va detto, altrimenti l'elenco vuoto sembra un
   * difetto. Quando l'elenco non c'e' proprio — `getDevices()` non esiste — si
   * spiega anche quello.
   */
  private renderDevices(view: ConnectionView): void {
    this.devicesRoot.append(el('p', 'section-title', 'Schede che questa pagina vede'));

    if (view.devices.length > 0) {
      this.devicesRoot.append(deviceList(view.devices));
    }

    if (!view.canListDevices) {
      this.devicesRoot.append(
        el(
          'p',
          'hint',
          "Questo browser non sa elencare le schede gia' autorizzate: qui sopra c'e' solo quella che questa pagina conosce. Nella finestra di scelta compaiono tutte quelle vicine.",
        ),
      );
      return;
    }

    if (view.devices.length === 0) {
      this.devicesRoot.append(
        el('p', 'hint', 'Nessuna: compariranno qui le schede scelte da questa pagina.'),
      );
    }
  }
}
