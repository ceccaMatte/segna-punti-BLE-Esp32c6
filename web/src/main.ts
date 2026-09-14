/**
 * La pagina: mette insieme collegamento, commissioning e tabellone.
 *
 * Qui sta il percorso completo, che e' quello che conta capire:
 *
 *   prima volta   scegli la scheda -> CLAIM con un token nuovo -> salvi
 *                 l'associazione -> leggi lo stato della partita -> ti iscrivi
 *
 *   volte dopo    ritrovi la scheda gia' autorizzata -> AUTH col token salvato
 *                 -> leggi e ti iscrivi
 *
 *   se il token   "associazione non piu' valida": qualcuno ha tenuto basso il
 *   non vale      piedino di commissioning e la scheda ha un'altra
 *                 associazione. Si rifa' il commissioning.
 *
 * Lo stato della partita non si calcola mai qui: si riceve e si mostra.
 */

import { PadelBleClient, bluetoothAvailable, listKnownDevices, type KnownDevice } from './ble/PadelBleClient';
import { PacketDiagnostics } from './ble/diagnostics';
import { shortIdFromValue } from './ble/hex';
import {
  CommissioningState,
  ResultCode,
  TOKEN_LENGTH,
  type CommissioningStatusPacket,
  type DeviceInfoPacket,
  type ScoreStatePacket,
} from './ble/protocol';
import { toView, type ScoreView } from './score/ScoreState';
import { CommissioningStorage, type Association } from './storage/CommissioningStorage';
import { ConnectionPanel, type DeviceRow } from './ui/ConnectionPanel';
import { DiagnosticsPanel } from './ui/DiagnosticsPanel';
import { ScoreboardPanel } from './ui/ScoreboardPanel';
import { StatusBand } from './ui/StatusBand';
import type { DataFacts, StatusFacts } from './ui/status';

/* -------------------------------------------------------------------------- */
/* Elementi della pagina                                                      */
/* -------------------------------------------------------------------------- */

function need(id: string): HTMLElement {
  const node = document.getElementById(id);
  if (node === null) {
    throw new Error(`elemento mancante nella pagina: ${id}`);
  }
  return node;
}

/* -------------------------------------------------------------------------- */
/* Stato della pagina                                                         */
/* -------------------------------------------------------------------------- */

const storage = new CommissioningStorage();
const diagnostics = new PacketDiagnostics();

let association: Association | null = storage.load();
let info: DeviceInfoPacket | null = null;
let status: CommissioningStatusPacket | null = null;
let score: ScoreView | null = null;
let message: string | null = null;
let busy = false;

/** Le schede che il browser lascia rivedere; null se non sa elencarle. */
let knownDevices: KnownDevice[] | null = null;

/** Token generato e in attesa che la scheda lo accetti. */
let pendingToken: Uint8Array | null = null;

/** Vero quando le notifiche della partita sono attive. */
let streaming = false;

/* -------------------------------------------------------------------------- */
/* Interfaccia                                                                */
/* -------------------------------------------------------------------------- */

const scoreboard = new ScoreboardPanel(need('score-content'));
const diagnosticsPanel = new DiagnosticsPanel(need('diagnostics-facts'));
const statusBand = new StatusBand(need('status-band'));

const connection = new ConnectionPanel(
  need('connection-status'),
  need('connection-facts'),
  need('connection-message'),
  need('connection-actions'),
  need('connection-devices'),
  {
    onCommission: () => void commission(),
    onReconnect: () => void reconnect(),
    onDisconnect: () => void client.disconnect(),
    onForget: forget,
  },
);

const client = new PadelBleClient({
  onConnected: () => {
    message = null;
    render();
  },
  onDisconnected: () => {
    diagnostics.noteDisconnect();
    streaming = false;
    render();
  },
  onStatus: (packet) => {
    handleStatus(packet);
  },
  onScore: (packet) => {
    applyScore(packet);
  },
  onError: (text) => {
    diagnostics.noteError(text);
    message = text;
    render();
  },
});

/* -------------------------------------------------------------------------- */
/* Quello che arriva dalla scheda                                             */
/* -------------------------------------------------------------------------- */

function handleStatus(packet: CommissioningStatusPacket): void {
  status = packet;

  switch (packet.result) {
    case ResultCode.ClaimSuccess: {
      /* Solo adesso si puo' salvare l'associazione: prima era una richiesta,
         adesso e' una cosa che la scheda ha accettato. */
      const token = pendingToken;
      pendingToken = null;
      if (token !== null) {
        saveAssociation(token);
      }
      message = 'Scheda associata.';
      void startStreaming();
      break;
    }

    case ResultCode.AuthSuccess:
      message = null;
      void startStreaming();
      break;

    case ResultCode.AuthFailed:
      /* La scheda ha un'altra associazione: questa non vale piu' niente. */
      storage.clear();
      association = null;
      streaming = false;
      message = "Associazione non piu' valida: serve un nuovo commissioning.";
      break;

    case ResultCode.ClaimRejected:
      pendingToken = null;
      message = 'Commissioning rifiutato: la finestra non era aperta.';
      break;

    case ResultCode.Timeout:
      message = 'La finestra di commissioning e\' scaduta senza concludere.';
      break;

    case ResultCode.ProtocolError:
      message = 'La scheda non ha capito il comando: sono due versioni diverse?';
      break;

    default:
      break;
  }

  render();
}

function applyScore(packet: ScoreStatePacket): void {
  diagnostics.notePacket(packet.sequence, Date.now());
  score = toView(packet);
  render();
}

/* -------------------------------------------------------------------------- */
/* Percorsi                                                                   */
/* -------------------------------------------------------------------------- */

async function startStreaming(): Promise<void> {
  if (streaming) {
    return;
  }
  streaming = true;

  try {
    /* Prima ci si iscrive e poi si legge: al contrario, un aggiornamento che
       arriva fra la lettura e l'iscrizione andrebbe perso. */
    await client.subscribeScore();
    applyScore(await client.readScore());
  } catch (error) {
    streaming = false;
    report(error);
  }
}

async function connectAndIdentify(device: BluetoothDevice | null): Promise<void> {
  diagnostics.reset();

  await client.connect(device);
  info = await client.readDeviceInfo();
  status = await client.readStatus();
  await client.subscribeStatus();

  if (association !== null) {
    /* L'esito del riconoscimento arriva come notifica, gestita da handleStatus. */
    await client.auth(association.token);
    return;
  }

  await askForClaim();
}

async function askForClaim(): Promise<void> {
  if (status?.state !== CommissioningState.WindowOpen) {
    message =
      "La finestra di commissioning non e' aperta: tieni il piedino GPIO0 verso massa per 3 secondi e riprova.";
    render();
    return;
  }

  /* Sedici byte presi dal generatore casuale del browser: sono l'associazione
     fra questa installazione della pagina e questa scheda. */
  pendingToken = crypto.getRandomValues(new Uint8Array(TOKEN_LENGTH));
  await client.claim(pendingToken);
}

async function commission(): Promise<void> {
  if (busy) {
    return;
  }
  busy = true;
  message = null;
  render();

  try {
    const device = await client.requestDevice();
    await connectAndIdentify(device);
  } catch (error) {
    report(error);
  }

  busy = false;
  render();
}

async function reconnect(): Promise<void> {
  if (busy) {
    return;
  }
  busy = true;
  message = null;
  render();

  try {
    /*
     * Se il browser sa ritrovare le schede gia' autorizzate, si prova da li'
     * senza far scegliere niente. Se non lo sa fare — in Chrome dipende da
     * un'impostazione sperimentale — si passa dalla finestra di scelta, che e'
     * l'unica strada che le regole del browser permettono.
     */
    const known =
      association !== null ? await client.findKnownDevice(association.browserDeviceId) : null;

    if (known !== null) {
      await connectAndIdentify(known);
    } else {
      const device = await client.requestDevice();
      await connectAndIdentify(device);
    }
  } catch (error) {
    report(error);
  }

  busy = false;
  render();
}

/** Prova a ricollegarsi da sola all'apertura della pagina, senza disturbare nessuno. */
async function tryQuietReconnect(): Promise<void> {
  if (association === null || !bluetoothAvailable()) {
    return;
  }

  try {
    const known = await client.findKnownDevice(association.browserDeviceId);
    if (known === null) {
      render();
      return;
    }
    await connectAndIdentify(known);
  } catch {
    /* Non e' un errore da mostrare: e' solo un tentativo. Se non riesce, il
       pulsante di riconnessione e' li' apposta. */
  }

  render();
}

function forget(): void {
  storage.clear();
  association = null;
  message =
    "Associazione locale dimenticata. Sulla scheda resta finche' non tieni basso GPIO0 per 3 secondi.";
  render();
}

function saveAssociation(token: Uint8Array): void {
  const browserDeviceId = client.deviceId;
  if (browserDeviceId === null) {
    return;
  }

  association = {
    browserDeviceId,
    shortId: info !== null ? shortIdFromValue(info.shortId) : '',
    deviceName: client.deviceName ?? '',
    token,
    savedAt: Date.now(),
  };

  storage.save(association);
}

function report(error: unknown): void {
  const text = error instanceof Error ? error.message : 'errore sconosciuto';
  diagnostics.noteError(text);
  message = text;
  render();
}

/* -------------------------------------------------------------------------- */
/* Disegno                                                                    */
/* -------------------------------------------------------------------------- */

function render(): void {
  const facts = statusFacts();
  const data = dataFacts();

  statusBand.render(facts, data, identLine());

  connection.render({
    supported: facts.supported,
    connected: facts.connected,
    authenticated: facts.authenticated,
    connectedName: facts.connectedName,
    knownName: facts.knownName,
    shortId:
      info !== null ? shortIdFromValue(info.shortId) : (association?.shortId ?? null),
    firmware: info !== null ? info.firmware : null,
    state: status !== null ? status.state : (info?.state ?? null),
    remainingSeconds: data.remainingSeconds,
    message,
    hasAssociation: association !== null,
    busy,
    devices: deviceRows(),
    canListDevices: knownDevices !== null,
  });

  scoreboard.render(score, !facts.connected);
  diagnosticsPanel.render(diagnostics, Date.now());
}

/** Quello che si sa del collegamento, come lo vuole `status.ts`. */
function statusFacts(): StatusFacts {
  return {
    supported: bluetoothAvailable(),
    connected: client.connected,
    authenticated: status?.authenticated ?? false,
    connectedName: client.deviceName,
    knownName: association?.deviceName ?? null,
    busy,
  };
}

/** Quello che si sa dei dati in arrivo. */
function dataFacts(): DataFacts {
  return {
    ageMs: diagnostics.ageMs(Date.now()),
    remainingSeconds:
      status !== null && status.remainingSeconds > 0 ? status.remainingSeconds : null,
  };
}

/** La riga che dice quale scheda si sta guardando. */
function identLine(): string | null {
  const nome = client.deviceName ?? association?.deviceName ?? null;
  if (nome === null) {
    return null;
  }

  const parti = [nome];
  const breve = info !== null ? shortIdFromValue(info.shortId) : (association?.shortId ?? null);

  if (breve !== null && breve !== '') {
    parti.push(`Device ${breve}`);
  }
  if (info !== null) {
    parti.push(`firmware ${info.firmware}`);
  }
  if (!client.connected) {
    parti.push('non collegata adesso');
  }

  return parti.join(' — ');
}

/**
 * Le schede da mostrare in elenco.
 *
 * Se il browser sa elencarle si mostrano quelle, altrimenti resta solo quella
 * che questa pagina conosce per conto suo: l'associazione salvata.
 */
function deviceRows(): DeviceRow[] {
  if (knownDevices === null) {
    if (association === null) {
      return [];
    }
    return [
      {
        name: association.deviceName,
        id: association.browserDeviceId,
        associated: true,
        connected: client.connected && client.deviceId === association.browserDeviceId,
      },
    ];
  }

  return knownDevices.map((device) => ({
    name: device.name,
    id: device.id,
    associated: association !== null && association.browserDeviceId === device.id,
    connected: client.connected && client.deviceId === device.id,
  }));
}

render();

/* L'eta' dell'ultimo aggiornamento cambia anche senza che arrivi niente: si
   rinfrescano solo la banda e la diagnostica, per non far sparire i pulsanti
   sotto il dito di chi sta per premere. */
setInterval(() => {
  statusBand.render(statusFacts(), dataFacts(), identLine());
  diagnosticsPanel.render(diagnostics, Date.now());
}, 1000);

void refreshKnownDevices();
void tryQuietReconnect();

/** Rilegge l'elenco delle schede note al browser. */
async function refreshKnownDevices(): Promise<void> {
  knownDevices = await listKnownDevices();
  render();
}
