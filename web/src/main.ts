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
 *   se cade il    la pagina riprova da sola, come fanno le cuffie quando le
 *   collegamento  riaccendi: prima ogni secondo, poi sempre piu' di rado. La
 *                 scheda puo' riavviarsi quanto vuole.
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
import { SilenceWatch } from './ble/liveness';
import { AutoReconnect } from './ble/reconnect';
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
import type { DataFacts, RetryFacts, StatusFacts } from './ui/status';

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

/** Il conto del silenzio: da quanto la scheda non si fa sentire. */
const silence = new SilenceWatch();

let association: Association | null = storage.load();
let info: DeviceInfoPacket | null = null;
let status: CommissioningStatusPacket | null = null;
let score: ScoreView | null = null;
let message: string | null = null;
let busy = false;

/** Le schede che il browser lascia rivedere; null se non sa elencarle. */
let knownDevices: KnownDevice[] | null = null;

/**
 * Vero finche' la pagina deve riprovare da sola a riprendere la scheda.
 *
 * Diventa falso quando e' l'utente a chiudere il collegamento (SCOLLEGA, o
 * ANNULLA RICONNESSIONE): riaprire un collegamento che qualcuno ha appena
 * chiuso sarebbe una sorpresa, e le sorprese con la radio non piacciono a
 * nessuno.
 */
let autoMode = true;

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
    onDisconnect: () => stopAndDisconnect(),
    onForget: forget,
    onStopRetry: stopRetry,
  },
);

const client = new PadelBleClient({
  onConnected: () => {
    message = null;
    /* Il conto del silenzio riparte da capo: la prima cosa che si aspetta e'
       il battito della scheda appena collegata. */
    silence.reset();
    /* Se prima c'era stata un'interruzione, si sa quanto e' durata. */
    diagnostics.noteReconnect(Date.now());
    /* Il collegamento c'e': il conto dei tentativi riparte da zero. */
    autoReconnect.connected();
    render();
  },
  onDisconnected: () => {
    diagnostics.noteDisconnect(Date.now());
    streaming = false;

    /*
     * La scheda si e' riavviata, o e' passato un disturbo: si riprova da soli.
     * Non si riprova se a chiudere e' stato l'utente, che e' quello che dice
     * `autoMode`, ne' se non c'e' un'associazione da usare per farsi
     * riconoscere.
     *
     * Il primo tentativo non parte subito: la radio ha appena perso il
     * collegamento e ha bisogno di un momento. Un secondo e' quello che ci
     * vuole.
     */
    if (autoMode && association !== null) {
      autoReconnect.start(false);
    }

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

/*
 * La riconnessione automatica sta qui, non nel client: il client sa aprire un
 * collegamento, ma non sa quante volte valga la pena riprovare. Come si
 * riprova lo decide `reconnect.ts`, dove si puo' anche provare.
 */
const autoReconnect = new AutoReconnect({
  attempt: () => attemptReconnect(),
  onChange: () => render(),
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
  const now = Date.now();
  silence.noteAlive(now);

  if (packet.heartbeat) {
    /*
     * Non e' successo niente: la scheda si e' fatta sentire. Serve a sapere che
     * c'e' — ed e' quello che permette di accorgersi in tre secondi che non c'e'
     * piu' — e a rinfrescare l'eta' dell'ultimo contatto.
     *
     * La pagina non si ridisegna: rifare i pulsanti sotto le dita di chi sta
     * per premere e' peggio di un tabellone che resta com'era.
     */
    diagnostics.noteHeartbeat(now);

    if (score === null) {
      /* Primo contatto: il battito porta comunque lo stato intero, e vale la
         pena di mostrarlo. */
      score = toView(packet);
      render();
    }
    return;
  }

  diagnostics.notePacket(packet.sequence, now);
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

/**
 * Riprova da sola a riprendere la scheda.
 *
 * E' il tentativo che la riconnessione automatica ripete finche' non riesce.
 * Non e' un tentativo vero quello che arriva mentre c'e' una scelta in corso:
 * c'e' una finestra di scelta aperta, e non ci si mette in mezzo.
 */
async function attemptReconnect(): Promise<boolean> {
  if (association === null) {
    /* Senza associazione non c'e' niente da riprendere: si smette, invece di
       provarci ogni quindici secondi per non concludere niente. */
    autoReconnect.stop();
    return false;
  }

  if (busy) {
    /* C'e' una scelta in corso, con la sua finestra aperta: non ci si mette in
       mezzo, ma il tentativo successivo resta in programma. */
    return false;
  }

  try {
    if (!client.hasDevice) {
      const known = await client.findKnownDevice(association.browserDeviceId);

      if (known === null) {
        /* Il browser non lascia riprendere una scheda senza un click, oppure
           non la conosce piu': insistere non servirebbe a niente, e la pagina
           lo dice invece di provarci per sempre. */
        autoReconnect.stop();
        message = 'Riprendere la scheda, con questo browser, richiede un click: premi RICONNETTI.';
        render();
        return false;
      }
    }

    await client.connect();
    await afterConnect();
    return true;
  } catch {
    /* Scheda spenta, lontana o ancora in avvio: si riprovera' fra poco. */
    return false;
  }
}

async function connectAndIdentify(device: BluetoothDevice | null): Promise<void> {
  diagnostics.reset();

  await client.connect(device);
  await afterConnect();
}

/** Quello che si fa con la scheda, appena il collegamento e' aperto. */
async function afterConnect(): Promise<void> {
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
  autoMode = true;
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
  autoMode = true;
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

/**
 * Scollega su richiesta dell'utente.
 *
 * E' l'unico caso in cui un collegamento si chiude e non si riapre da solo: chi
 * ha premuto sa quello che vuole, e la pagina non deve fare la spiritosa.
 */
function stopAndDisconnect(): void {
  autoMode = false;
  autoReconnect.stop();
  void client.disconnect();
}

/** L'utente non vuole aspettare che la scheda risponda: si smette di riprovare. */
function stopRetry(): void {
  autoMode = false;
  autoReconnect.stop();
  message =
    'Riconnessione automatica fermata. Il collegamento si riprende con RICONNETTI.';
  render();
}

function forget(): void {
  autoMode = false;
  autoReconnect.stop();
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
  const retry = retryFacts();

  statusBand.render(facts, data, retry, identLine());

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
    retry,
    canAutoReconnect: client.hasDevice || knownDevices !== null,
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

/** Quello che sta facendo la riconnessione automatica, per la pagina. */
function retryFacts(): RetryFacts {
  const view = autoReconnect.view();

  return {
    retrying: view.enabled && !client.connected,
    attempting: view.attempting,
    attempts: view.attempts,
    nextInSeconds:
      view.nextAttemptAtMs !== null
        ? Math.max(0, Math.ceil((view.nextAttemptAtMs - Date.now()) / 1000))
        : null,
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

/* L'eta' dell'ultimo aggiornamento e il conto alla rovescia dei tentativi
   cambiano anche senza che arrivi niente: si rinfrescano solo la banda e la
   diagnostica, per non far sparire i pulsanti sotto il dito di chi sta per
   premere. */
setInterval(() => {
  statusBand.render(statusFacts(), dataFacts(), retryFacts(), identLine());
  diagnosticsPanel.render(diagnostics, Date.now());
}, 1000);

/*
 * Il controllo del battito.
 *
 * Gira piu' spesso del disegno perche' da lui dipende quanto ci si mette ad
 * accorgersi che la scheda non c'e' piu'. Scaduto il silenzio il collegamento
 * si chiude a mano: un `disconnect()` esplicito non aspetta la supervisione del
 * Bluetooth, che da sola impiegherebbe una decina di secondi. Da li' in poi fa
 * tutto la riconnessione automatica.
 */
setInterval(() => {
  if (!client.connected || status?.authenticated !== true) {
    return;
  }

  if (silence.expired(Date.now())) {
    message = 'La scheda non risponde piu\': chiudo il collegamento e la riprendo.';
    silence.reset();
    void client.disconnect();
  }
}, 500);

/*
 * Tornando sulla scheda del browser puo' essere passato un pezzo: se in quel
 * frattempo il collegamento e' caduto, non ha senso aspettare il turno del
 * timer, che magari e' lontano quindici secondi.
 */
document.addEventListener('visibilitychange', () => {
  if (
    document.visibilityState === 'visible' &&
    autoMode &&
    association !== null &&
    !client.connected
  ) {
    autoReconnect.retryNow();
  }
});

/*
 * All'apertura della pagina si prova a riprendere la scheda da soli, come fa
 * un paio di cuffie quando le riaccendi: l'associazione dice quale scheda e
 * con quale token, e da li' in poi e' la pagina a insistere.
 */
if (association !== null) {
  autoReconnect.start(true);
}

void refreshKnownDevices();

/** Rilegge l'elenco delle schede note al browser. */
async function refreshKnownDevices(): Promise<void> {
  knownDevices = await listKnownDevices();
  render();
}
