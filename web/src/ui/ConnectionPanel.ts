/**
 * Pannello del collegamento e del commissioning.
 *
 * Mostra a che punto siamo e mette a disposizione i quattro pulsanti che
 * servono. Non prende decisioni: lo stato gli arriva gia' pronto da fuori, e i
 * pulsanti si limitano a chiamare chi sa cosa fare.
 */

import { CommissioningState } from '../ble/protocol';
import { button, clear, el, facts, statusLine } from './dom';

export interface ConnectionView {
  supported: boolean;
  connected: boolean;
  deviceName: string | null;
  shortId: string | null;
  state: CommissioningState | null;
  authenticated: boolean;
  remainingSeconds: number | null;
  /** Riga di spiegazione sotto lo stato. */
  message: string | null;
  /** Vero se il browser ricorda un'associazione. */
  hasAssociation: boolean;
  /** Vero mentre una richiesta e' in corso: i pulsanti si spengono. */
  busy: boolean;
}

export interface ConnectionActions {
  onCommission: () => void;
  onReconnect: () => void;
  onDisconnect: () => void;
  onForget: () => void;
}

function stateText(view: ConnectionView): string {
  if (!view.connected) {
    return view.hasAssociation ? 'Non collegata' : 'Nessuna scheda collegata';
  }
  if (view.authenticated) {
    return 'Collegata e riconosciuta';
  }
  if (view.state === CommissioningState.WindowOpen) {
    return 'Collegata, in attesa di associazione';
  }
  return 'Collegata, non riconosciuta';
}

function stateKind(view: ConnectionView): 'ok' | 'warn' | 'off' {
  if (view.connected && view.authenticated) {
    return 'ok';
  }
  if (view.connected) {
    return 'warn';
  }
  return 'off';
}

export class ConnectionPanel {
  private readonly actions: ConnectionActions;

  constructor(
    private readonly statusRoot: HTMLElement,
    private readonly factsRoot: HTMLElement,
    private readonly messageRoot: HTMLElement,
    private readonly buttonsRoot: HTMLElement,
    actions: ConnectionActions,
  ) {
    this.actions = actions;
  }

  render(view: ConnectionView): void {
    clear(this.statusRoot);
    clear(this.factsRoot);
    clear(this.messageRoot);
    clear(this.buttonsRoot);

    if (!view.supported) {
      this.statusRoot.append(
        statusLine('off', 'Questo browser non ha Web Bluetooth: servono Chrome o Edge su computer.'),
      );
      return;
    }

    this.statusRoot.append(statusLine(stateKind(view), stateText(view)));

    const rows: Array<[string, string]> = [];
    rows.push(['Scheda', view.deviceName ?? '—']);
    rows.push(['Nome breve', view.shortId ?? '—']);
    rows.push([
      'Associazione',
      view.state === CommissioningState.Commissioned ? 'presente' : 'assente',
    ]);

    if (view.remainingSeconds !== null && view.remainingSeconds > 0) {
      rows.push(['Finestra aperta', `${view.remainingSeconds} s`]);
    }

    this.factsRoot.append(facts(rows));

    if (view.message !== null && view.message !== '') {
      this.messageRoot.append(el('p', 'message', view.message));
    }

    /* I pulsanti compaiono solo quando servono: e' piu' facile capire cosa si
       puo' fare guardando cosa c'e' scritto. */
    if (view.busy) {
      this.buttonsRoot.append(button('Attendere…', () => {}, { disabled: true }));
      return;
    }

    if (!view.connected) {
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
}
