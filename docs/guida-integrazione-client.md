# Collegarsi alla scheda da una pagina web — guida per chi scrive un client

Questo documento è **autosufficiente**: contiene tutto quello che serve per
scrivere un programma (una pagina web, un pannello di regia, un'app) che trovi
la scheda, si associ, e riceva da lei il punteggio della partita.

Il codice di riferimento da cui sono nate queste pagine è in `web/`: leggerlo
aiuta, ma non è obbligatorio. La regola che vale sopra tutte è una sola:

> **La scheda è l'unica sorgente di verità.**
> Il client non calcola punti, game, set, undo o tie-break: riceve lo stato
> completo dopo ogni gesto e lo mostra. Se il client calcolasse qualcosa,
> prima o poi i due tabelloni racconterebbero due partite diverse.

---

## 1. In trenta secondi

- La scheda si annuncia via Bluetooth con il nome **`PADEL_SCORE_XXXX`**
  (le quattro cifre sono la parte finale del suo indirizzo di rete).
- Offre **un servizio GATT** con quattro characteristic: chi è, com'è la
  partita, lo stato dell'associazione, e i comandi per associarsi.
- Il punteggio viaggia in **notifiche da 17 byte**, sempre intero (mai solo
  "la notizia"): un pacchetto perso non fa perdere l'allineamento, il
  successivo rimette tutto a posto.
- Prima di ricevere il punteggio bisogna **associarsi** (una volta sola):
  si genera un token di 16 byte e si consegna alla scheda mentre ha la
  **finestra di commissioning** aperta. Dopo, ci si fa riconoscere con lo
  stesso token.
- Il punteggio **non si salva** da nessuna parte: si azzera al riavvio.
  L'associazione invece sopravvive al riavvio e alla mancanza di corrente.

---

## 2. Cosa serve per parlare con la scheda

| Requisito | Dettaglio |
|---|---|
| Browser | Chrome, Edge od Opera (desktop o Android). **Web Bluetooth non esiste su Firefox e Safari.** |
| Contesto sicuro | `https://` oppure `http://localhost`. Una pagina aperta con `file://` non funziona. |
| Gesto dell'utente | `navigator.bluetooth.requestDevice()` **deve** partire da un click: il browser apre una finestra di scelta e non si può evitare. |
| Server | Nessuno. Non c'è backend, non ci sono account: tutto accade nel browser. |
| Connessioni | **Una sola per volta.** La scheda smette di annunciarsi finché un client è collegato. |

---

## 3. La mappa del servizio GATT

Un servizio, quattro characteristic. Gli UUID sono fissi e **non cambiano**:
sono scritti nella scheda (`main/link/ble_protocol.h`) e nel client di
riferimento (`web/src/ble/protocol.ts`).

| Characteristic | UUID | Proprietà | Chi può leggerla |
|---|---|---|---|
| *Servizio* | `6b8d0001-9c4f-4e21-b7a3-0d5e1f2a3b40` | — | — |
| `DEVICE_INFO` | `6b8d0002-9c4f-4e21-b7a3-0d5e1f2a3b40` | READ | tutti |
| `SCORE_STATE` | `6b8d0003-9c4f-4e21-b7a3-0d5e1f2a3b40` | READ + NOTIFY | **solo dopo il riconoscimento** |
| `COMMISSION_CONTROL` | `6b8d0004-9c4f-4e21-b7a3-0d5e1f2a3b40` | WRITE | tutti (comandi) |
| `COMMISSION_STATUS` | `6b8d0005-9c4f-4e21-b7a3-0d5e1f2a3b40` | READ + NOTIFY | tutti |

Due cose da sapere prima di scrivere una riga di codice:

- **L'annuncio contiene l'UUID del servizio, non il nome.** Il nome sta nella
  risposta allo scan, che il browser legge comunque. Per questo si filtra sul
  servizio — il nome è solo un aiuto per l'occhio nella finestra di scelta:
  ```ts
  navigator.bluetooth.requestDevice({
    filters: [{ services: [SERVICE_UUID] }],
    optionalServices: [SERVICE_UUID],
  });
  ```
- **Non esiste bonding né PIN.** L'associazione è un token applicativo:
  niente SMP, niente chiavi scambiate dallo stack. Chi ha il token legge il
  punteggio, chi non ce l'ha non lo legge.

---

## 4. L'associazione (commissioning), passo per passo

La scheda può trovarsi in tre stati, che si leggono nel byte 2 di
`DEVICE_INFO` e di `COMMISSIONING_STATUS`:

| Valore | Stato | Cosa significa per il client |
|---|---|---|
| `0` | `UNCOMMISSIONED` | nessuna associazione salvata: serve un `CLAIM` |
| `1` | `WINDOW_OPEN` | la finestra è aperta per **60 secondi**: si può fare `CLAIM` |
| `2` | `COMMISSIONED` | c'è un'associazione: ci si fa riconoscere con `AUTH` |

### Quando si apre la finestra

Solo con un gesto **fisico** sulla scheda (un client non può aprirla):

- **pulsante BOOT tenuto premuto per 5 secondi** (è anche un gesto di gioco);
- **piedino GPIO0 verso massa per 3 secondi** (morsetto o filo).

Vale anche la prima volta, su una scheda mai associata: lo stato
`UNCOMMISSIONED` dice che non c'è nessun token salvato, **non** che la finestra
sia aperta. Finché non la si apre, un `CLAIM` viene rifiutato.

Aprire la finestra **cancella l'associazione precedente**: è una revoca, ed è
voluta. La finestra resta aperta 60 secondi, poi si richiude da sola.

### Il percorso del client

```
click dell'utente
   ↓ requestDevice()                       ← solo da un gesto
   ↓ device.gatt.connect()
   ↓ getPrimaryService(SERVICE_UUID)
   ↓ read DEVICE_INFO + read COMMISSIONING_STATUS
   ↓
   ho un token salvato?
      ├── sì → write AUTH(token)
      │        ├── AUTH_SUCCESS → sono dentro: leggo e ascolto il punteggio
      │        └── AUTH_FAILED  → il token non vale più: lo cancello e chiedo
      │                            all'utente di riaprire la finestra
      └── no → la finestra è aperta?
               ├── sì → genero 16 byte casuali → write CLAIM(token)
               │        ├── CLAIM_SUCCESS → salvo il token e ascolto il punteggio
               │        └── CLAIM_REJECTED → la finestra si è chiusa: riprovare
               └── no → chiedo all'utente di tenere premuto BOOT per 5 secondi
```

**Regola d'oro della risposta**: il risultato di `CLAIM` e `AUTH` arriva come
**notifica su `COMMISSIONING_STATUS`**. Iscriversi alle notifiche *prima* di
scrivere il comando, altrimenti la risposta si perde. In mancanza, si può
rileggere la characteristic dopo la scrittura.

### I risultati possibili

| Valore | Nome | Cosa vuol dire | Cosa fare |
|---|---|---|---|
| `0` | `IDLE` | niente da segnalare | — |
| `1` | `CLAIM_SUCCESS` | associazione creata | salvare il token e proseguire |
| `2` | `AUTH_SUCCESS` | token riconosciuto | proseguire |
| `3` | `AUTH_FAILED` | il token non è quello salvato | cancellare il token locale, chiedere una nuova finestra |
| `4` | `CLAIM_REJECTED` | `CLAIM` arrivato fuori dalla finestra | chiedere all'utente di riaprirla |
| `5` | `TIMEOUT` | finestra scaduta senza committenti | idem |
| `6` | `PROTOCOL_ERROR` | comando illeggibile | controllare la versione del protocollo |

Il risultato è uno **stato**, non un evento: resta lì finché non succede
qualcos'altro. Per questo conviene guardarlo insieme alla notifica, non da solo.

### Cosa cancellare l'associazione (e far fallire l'`AUTH`)

- il pulsante BOOT tenuto 5 secondi;
- il piedino GPIO0 tenuto basso 3 secondi;
- un nuovo `CLAIM` da parte di un altro client (sovrascrive il token).

### Il caso `START_PAIRING`

Se il client è collegato e riconosciuto quando qualcuno tiene premuto il
pulsante, riceve **prima** un pacchetto con `event = 5` (`START_PAIRING`) e
**poi** la scheda cancella l'associazione e apre la finestra. È l'ultimo
messaggio utile: dopo, il token salvato non vale più. Il client dovrebbe
mostrare un avviso ("la scheda sta entrando in commissioning") e prepararsi a
rifare l'associazione.

---

## 5. I pacchetti, byte per byte

Tutti i numeri più lunghi di un byte viaggiano **dal byte meno significativo**
(little-endian), come li scrive l'ESP32 e come li legge
`DataView.getUint16(offset, true)`.

Ogni pacchetto comincia con due byte di testa:

| Byte | Cosa |
|---|---|
| 0 | versione del protocollo (`1`) |
| 1 | tipo di messaggio (`1` punteggio, `2` associazione, `3` scheda) |

**Un pacchetto con versione diversa va scartato**, non interpretato: un
pacchetto capito a metà è peggio di un pacchetto perso.

### `SCORE_STATE` — 17 byte (tipo `1`)

| Byte | Cosa |
|---|---|
| 0 | versione (`1`) |
| 1 | tipo (`1`) |
| 2 | flag: bit 0 tie-break, bit 1 partita finita, bit 2 serve NOI, bit 3 battito |
| 3 | vincitore: `0` LORO, `1` NOI, `0xFF` nessuno |
| 4..5 | numero di sequenza |
| 6 | punti LORO |
| 7 | punti NOI |
| 8 | game LORO |
| 9 | game NOI |
| 10 | set LORO |
| 11 | set NOI |
| 12..13 | punti del tie-break LORO |
| 14..15 | punti del tie-break NOI |
| 16 | **evento**: che cosa ha provocato l'invio |

Note:

- **indice 0 = LORO** (pannello di sinistra, verde), **indice 1 = NOI**
  (pannello di destra, azzurro);
- i punti del game sono i valori del motore: `0` = 0, `1` = 15, `2` = 30,
  `3` = 40, `4` = vantaggio (si scrive "AD");
- **durante il tie-break i punti del game restano a zero**: i punti veri sono
  quelli contati uno per uno nei byte 12..15, e si guardano solo se il bit 0
  dei flag è alzato;
- il campo `vincitore` ha senso solo se il bit 1 dei flag è alzato.

### Gli eventi (byte 16)

| Valore | Nome | Chi lo produce |
|---|---|---|
| `0` | `NONE` | nessun gesto: un battito, o uno stato cambiato da solo |
| `1` | `OUR_POINT` | 1 click del pulsante |
| `2` | `THEIR_POINT` | 2 click |
| `3` | `UNDO` | 3 click |
| `4` | `MOMENT` | pressione lunga rilasciata: **un segno nel tempo, il punteggio non cambia** |
| `5` | `START_PAIRING` | sta per aprirsi una nuova finestra di commissioning |
| `6` | `RESET` | 4 click: partita azzerata |
| `7` | `STATE_SYNC` | non è un gesto: "questo è lo stato adesso" |

**La semantica, in una riga**: l'evento dice *perché* è arrivato il pacchetto,
tutto il resto dice com'è la partita *dopo* quel gesto. Il client **non applica
niente**: disegna quello che legge. Esempio: arriva `event = 1` e
`punti NOI = 1`: il punteggio da mostrare è già quello, non c'è nessun punto
da aggiungere.

### Il battito (flag bit 3)

Ogni **1,2 secondi** la scheda manda lo **stesso** stato di prima, con il bit
del battito alzato, lo **stesso numero di sequenza** e `event = 0`. Non è un
aggiornamento: serve a far sapere che la scheda c'è, e a distinguere "partita
ferma" da "scheda sparita". Chi lo riceve non deve ridisegnare il tabellone
(se non quando è arrivato il primo in assoluto).

### Notifiche e letture non dicono la stessa cosa

| Come arriva | Campo `event` | Perché |
|---|---|---|
| notifica | il gesto appena avvenuto | racconta cosa è successo adesso |
| risposta a una **lettura** | sempre `STATE_SYNC` (7) | chi legge chiede com'è la partita, e non deve vedere un gesto vecchio come se fosse di adesso |

Quindi: le notifiche si ascoltano, le letture si usano per allinearsi appena
connessi.

### `COMMISSIONING_STATUS` — 6 byte (tipo `2`)

| Byte | Cosa |
|---|---|
| 0 | versione (`1`) |
| 1 | tipo (`2`) |
| 2 | stato dell'associazione: `0`/`1`/`2` come sopra |
| 3 | `1` se **questa** connessione si è fatta riconoscere |
| 4 | esito dell'ultima operazione (tabella sopra) |
| 5 | secondi che restano alla finestra, `0` se chiusa |

### `DEVICE_INFO` — 8 byte (tipo `3`)

| Byte | Cosa |
|---|---|
| 0 | versione (`1`) |
| 1 | tipo (`3`) |
| 2 | stato dell'associazione |
| 3 | `1` se questa connessione è riconosciuta |
| 4..5 | versione del firmware (byte alto = maggiore; `0x0100` = 1.0) |
| 6..7 | nome breve come numero: le quattro cifre di `PADEL_SCORE_XXXX` |

### `COMMISSION_CONTROL` — 17 byte (si scrive)

| Byte | Cosa |
|---|---|
| 0 | opcode: `0x01` = `CLAIM`, `0x02` = `AUTH` |
| 1..16 | token di 16 byte |

Si scrive con **`writeValueWithResponse()`**: la scrittura con risposta arriva
quando la scheda ha preso in carico il comando, e un'associazione vale qualche
millisecondo di attesa in più.

---

## 6. Il ritmo: cosa aspettarsi e quando

| Momento | Cosa arriva |
|---|---|
| dopo un `AUTH` o un `CLAIM` riuscito | subito uno `STATE_SYNC` con lo stato completo, anche a partita ferma |
| a ogni gesto del pulsante | un pacchetto con l'evento e lo stato aggiornato |
| quando lo stato cambia da solo (fine della schermata del vincitore) | un pacchetto con `event = 0` |
| ogni 1,2 s, se non succede niente | il battito |

Il **numero di sequenza** avanza di uno a ogni pubblicazione vera (il battito
lo lascia fermo). Serve alla diagnostica del client: numeri consecutivi =
tutto bene, un salto = una notifica persa, un numero ripetuto = è arrivata
due volte.

### Il collegamento è morto? Non aspettare il sistema operativo

Quando la scheda si riavvia o si allontana, il collegamento muore **senza
avvisare**: il computer se ne accorge solo dopo il tempo di supervisione, che
sul desktop è di una decina di secondi. La scheda chiede 2 secondi, ma non è
garantito.

La via d'uscita è il battito: **se non arriva niente per 3 secondi, il
collegamento è morto**. A quel punto conviene chiudere il collegamento a mano
(`device.gatt.disconnect()`) — un `disconnect()` esplicito lo fa cadere
subito, senza aspettare la supervisione — e riconnettersi, rifacendo `AUTH`.
Sotto i cinque secondi si torna a mostrare il punteggio.

---

## 7. Un esempio completo

Questo è un client minimo ma completo: si sceglie la scheda, ci si associa se
serve, ci si fa riconoscere, e si segue il punteggio. Le costanti e i
decodificatori sono gli stessi del client di riferimento.

```ts
/* ------------------------------------------------------------------ */
/* Costanti del protocollo                                            */
/* ------------------------------------------------------------------ */

const SERVICE_UUID = '6b8d0001-9c4f-4e21-b7a3-0d5e1f2a3b40';
const DEVICE_INFO_UUID = '6b8d0002-9c4f-4e21-b7a3-0d5e1f2a3b40';
const SCORE_STATE_UUID = '6b8d0003-9c4f-4e21-b7a3-0d5e1f2a3b40';
const CONTROL_UUID = '6b8d0004-9c4f-4e21-b7a3-0d5e1f2a3b40';
const STATUS_UUID = '6b8d0005-9c4f-4e21-b7a3-0d5e1f2a3b40';

const PROTOCOL_VERSION = 1;
const TOKEN_BYTES = 16;

enum Op { Claim = 0x01, Auth = 0x02 }

enum CommState { Uncommissioned = 0, WindowOpen = 1, Commissioned = 2 }

enum Result {
  Idle = 0, ClaimSuccess = 1, AuthSuccess = 2, AuthFailed = 3,
  ClaimRejected = 4, Timeout = 5, ProtocolError = 6,
}

enum PadelEvent {
  None = 0, OurPoint = 1, TheirPoint = 2, Undo = 3,
  Moment = 4, StartPairing = 5, Reset = 6, StateSync = 7,
}

/* ------------------------------------------------------------------ */
/* I pacchetti, letti dove sono stati scritti                         */
/* ------------------------------------------------------------------ */

interface Status {
  state: CommState;
  authenticated: boolean;
  result: Result;
  remainingSeconds: number;
}

interface MatchState {
  sequence: number;
  event: PadelEvent;
  heartbeat: boolean;
  tieBreak: boolean;
  finished: boolean;
  servingNoi: boolean;
  winner: 0 | 1 | null;
  /** Valori del motore: 0, 1, 2, 3, 4 (15, 30, 40, vantaggio). */
  points: [number, number];
  games: [number, number];
  sets: [number, number];
  tieBreakPoints: [number, number];
}

function decodeStatus(dv: DataView): Status | null {
  if (dv.byteLength < 6) return null;
  if (dv.getUint8(0) !== PROTOCOL_VERSION || dv.getUint8(1) !== 2) return null;

  return {
    state: dv.getUint8(2) as CommState,
    authenticated: dv.getUint8(3) !== 0,
    result: dv.getUint8(4) as Result,
    remainingSeconds: dv.getUint8(5),
  };
}

function decodeScore(dv: DataView): MatchState | null {
  if (dv.byteLength < 17) return null;
  if (dv.getUint8(0) !== PROTOCOL_VERSION || dv.getUint8(1) !== 1) return null;

  const flags = dv.getUint8(2);
  const winner = dv.getUint8(3);

  return {
    sequence: dv.getUint16(4, true),
    event: dv.getUint8(16) as PadelEvent,
    heartbeat: (flags & 0x08) !== 0,
    tieBreak: (flags & 0x01) !== 0,
    finished: (flags & 0x02) !== 0,
    servingNoi: (flags & 0x04) !== 0,
    winner: winner === 0xff ? null : (winner as 0 | 1),
    points: [dv.getUint8(6), dv.getUint8(7)],
    games: [dv.getUint8(8), dv.getUint8(9)],
    sets: [dv.getUint8(10), dv.getUint8(11)],
    tieBreakPoints: [dv.getUint16(12, true), dv.getUint16(14, true)],
  };
}

/** Il comando: primo byte l'opcode, poi i sedici del token. */
function controlPacket(op: Op, token: Uint8Array): Uint8Array<ArrayBuffer> {
  if (token.length !== TOKEN_BYTES) throw new Error('il token deve essere di 16 byte');

  const out = new Uint8Array(new ArrayBuffer(1 + TOKEN_BYTES));
  out[0] = op;
  out.set(token, 1);
  return out;
}

/** Il punteggio da scrivere a video: traduzione, non calcolo. */
function pointLabel(points: number, tieBreak: boolean, tb: number): string {
  if (tieBreak) return String(tb);
  return ['0', '15', '30', '40', 'AD'][points] ?? '?';
}

/* ------------------------------------------------------------------ */
/* Il client                                                          */
/* ------------------------------------------------------------------ */

class PadelClient {
  private server!: BluetoothRemoteGATTServer;
  private control!: BluetoothRemoteGATTCharacteristic;
  private status!: BluetoothRemoteGATTCharacteristic;
  private score!: BluetoothRemoteGATTCharacteristic;

  /**
   * Scelta e collegamento. Da chiamare da un click: il browser vuole un gesto.
   */
  async connect(): Promise<BluetoothDevice> {
    const device = await navigator.bluetooth.requestDevice({
      filters: [{ services: [SERVICE_UUID] }],
      optionalServices: [SERVICE_UUID],
    });

    if (!device.gatt) throw new Error('questa scheda non offre GATT');

    this.server = await device.gatt.connect();
    const service = await this.server.getPrimaryService(SERVICE_UUID);

    this.control = await service.getCharacteristic(CONTROL_UUID);
    this.status = await service.getCharacteristic(STATUS_UUID);
    this.score = await service.getCharacteristic(SCORE_STATE_UUID);

    return device;
  }

  /** Legge com'è messa l'associazione adesso. */
  async readStatus(): Promise<Status> {
    const dv = await this.status.readValue();
    const status = decodeStatus(dv);
    if (!status) throw new Error('pacchetto di stato non valido');
    return status;
  }

  /**
   * Aspetta il prossimo stato dell'associazione, con un limite di tempo.
   *
   * Va armato *prima* di scrivere il comando: la risposta arriva come
   * notifica, e una notifica che arriva mentre nessuno ascolta è persa.
   */
  private waitStatus(timeoutMs = 2000): Promise<Status> {
    return new Promise((resolve, reject) => {
      const onChange = (): void => {
        const value = this.status.value;
        if (!value) return;
        const status = decodeStatus(value);
        if (!status) return;
        cleanup();
        resolve(status);
      };

      const timer = setTimeout(() => {
        cleanup();
        reject(new Error('la scheda non ha risposto in tempo'));
      }, timeoutMs);

      const cleanup = (): void => {
        clearTimeout(timer);
        this.status.removeEventListener('characteristicvaluechanged', onChange);
      };

      this.status.addEventListener('characteristicvaluechanged', onChange);
    });
  }

  /** Si fa riconoscere; falso se il token non vale più. */
  async authenticate(token: Uint8Array): Promise<boolean> {
    await this.status.startNotifications();

    const wait = this.waitStatus();
    await this.control.writeValueWithResponse(controlPacket(Op.Auth, token));
    const status = await wait;

    return status.result === Result.AuthSuccess;
  }

  /** Si associa. Il token dev'essere nuovo: sedici byte casuali. */
  async claim(token: Uint8Array): Promise<boolean> {
    await this.status.startNotifications();

    const wait = this.waitStatus();
    await this.control.writeValueWithResponse(controlPacket(Op.Claim, token));
    const status = await wait;

    return status.result === Result.ClaimSuccess;
  }

  /** Lo stato della partita, per allinearsi appena connessi. */
  async readScore(): Promise<MatchState> {
    const dv = await this.score.readValue();
    const state = decodeScore(dv);
    if (!state) throw new Error('pacchetto della partita non valido');
    return state;
  }

  /** Ascolta il punteggio. Il callback riceve lo stato a ogni pubblicazione. */
  async followScore(onState: (state: MatchState) => void): Promise<void> {
    await this.score.startNotifications();

    this.score.addEventListener('characteristicvaluechanged', (event) => {
      const target = event.target as BluetoothRemoteGATTCharacteristic;
      const value = target.value;
      if (!value) return;

      const state = decodeScore(value);
      if (state) onState(state);
    });
  }
}

/* ------------------------------------------------------------------ */
/* Il percorso completo                                               */
/* ------------------------------------------------------------------ */

const TOKEN_KEY = 'padel.token';

function loadToken(): Uint8Array | null {
  const hex = localStorage.getItem(TOKEN_KEY);
  if (hex === null || hex.length !== TOKEN_BYTES * 2) return null;

  const out = new Uint8Array(TOKEN_BYTES);
  for (let i = 0; i < TOKEN_BYTES; i += 1) {
    out[i] = Number.parseInt(hex.slice(i * 2, i * 2 + 2), 16);
  }
  return out;
}

function saveToken(token: Uint8Array): void {
  localStorage.setItem(TOKEN_KEY, Array.from(token, (b) => b.toString(16).padStart(2, '0')).join(''));
}

async function main(onState: (state: MatchState) => void): Promise<void> {
  const client = new PadelClient();
  await client.connect();

  const status = await client.readStatus();
  const token = loadToken();

  if (token !== null) {
    /* C'è un token: ci si fa riconoscere. Questo è il caso normale, tutte le
       volte dopo la prima. */
    const ok = await client.authenticate(token);

    if (!ok) {
      /* La scheda ha un'altra associazione: il token locale non vale più.
         Si cancella e si chiede all'utente di riaprire la finestra tenendo
         premuto il pulsante per cinque secondi. */
      localStorage.removeItem(TOKEN_KEY);
      throw new Error('associazione non più valida: tieni premuto BOOT per 5 secondi');
    }
  } else {
    if (status.state !== CommState.WindowOpen) {
      throw new Error('la scheda non è in commissioning: tieni premuto BOOT per 5 secondi');
    }

    const fresh = crypto.getRandomValues(new Uint8Array(TOKEN_BYTES));
    const ok = await client.claim(fresh);

    if (!ok) throw new Error('associazione rifiutata: la finestra si è chiusa');
    saveToken(fresh);
  }

  /* Da qui in avanti il punteggio arriva da solo. La lettura serve solo ad
     allinearsi subito, senza aspettare la prima pubblicazione. */
  onState(await client.readScore());
  await client.followScore(onState);
}

document.getElementById('connect')?.addEventListener('click', () => {
  void main((state) => {
    const loro = pointLabel(state.points[0], state.tieBreak, state.tieBreakPoints[0]);
    const noi = pointLabel(state.points[1], state.tieBreak, state.tieBreakPoints[1]);
    console.log(`LORO ${loro} - ${noi} NOI | game ${state.games[0]}-${state.games[1]} | event ${state.event}`);
  }).catch((error: unknown) => {
    console.error(error);
  });
});
```

---

## 8. Quando qualcosa non va

| Sintomo | Causa quasi sempre | Rimedio |
|---|---|---|
| `requestDevice` non parte | la chiamata non nasce da un click | spostarla dentro l'handler del click |
| La scheda non compare nella finestra | filtro sbagliato, oppure è già collegata a un altro client | filtrare sul servizio; chiudere l'altra connessione |
| `NotFoundError` | l'utente ha annullato la scelta | riprovare al click successivo |
| La lettura di `SCORE_STATE` fallisce | la connessione non si è ancora fatta riconoscere | fare `AUTH` prima |
| `AUTH_FAILED` | il token locale non è più quello della scheda | cancellare il token e riaprire la finestra (BOOT 5 s) |
| `CLAIM_REJECTED` | la finestra dei 60 secondi era chiusa | riaprirla e riprovare |
| `PROTOCOL_ERROR` | versioni diverse tra client e scheda | allineare le costanti, non indovinare |
| Il tabellone non si aggiorna più, ma "sembra collegato" | la scheda si è riavviata: il sistema non se n'è ancora accorto | contare il battito: 3 secondi di silenzio = chiudere e ricollegarsi |
| Il punteggio sembra indietro di un punto | si sta disegnando l'evento invece dello stato | usare solo i campi dello stato ricevuto |

---

## 9. Cosa NON fare (e perché)

- **Non calcolare mai il punteggio.** Niente "se arriva OUR_POINT aggiungo 15":
  lo stato nel pacchetto è già quello dopo il gesto.
- **Non inventare comandi.** La characteristic di controllo accetta solo
  `CLAIM` e `AUTH`, 17 byte esatti; qualunque altra cosa è un errore di
  protocollo.
- **Non attendere il nome nell'annuncio**: è nella risposta allo scan; si
  filtra sull'UUID del servizio, il nome è solo un aiuto.
- **Non aprire più connessioni in parallelo** sulla stessa scheda.
- **Non fidarsi della sola supervisione del sistema operativo** per accorgersi
  di una caduta: si usa il battito.
- **Non aspettarsi che il punteggio sopravviva al riavvio**: la scheda riparte
  da 0-0, l'associazione invece resta.
- **Non leggere `SCORE_STATE` prima del riconoscimento** e non stupirsi se la
  scheda, mentre è associata, dice di no a chiunque non abbia il token.

---

## 10. Dove sta già tutto questo, in questo repository

| File | Cosa contiene |
|---|---|
| `web/src/ble/protocol.ts` | UUID, tipi dei pacchetti, codifica e decodifica, nomi degli eventi |
| `web/src/ble/PadelBleClient.ts` | il collegamento: scelta della scheda, characteristic, notifiche, comandi |
| `web/src/ble/liveness.ts` | il conto del battito: quanto silenzio vuol dire "scheda sparita" |
| `web/src/ble/reconnect.ts` | la riconnessione automatica, con i suoi tempi |
| `web/src/main.ts` | il percorso completo: commissioning, riconnessione, tabellone, diagnostica |
| `web/test/protocol.test.ts` | gli stessi esempi di byte usati dai test del firmware |
| `docs/ble-architecture.md` | il perché delle scelte (protocollo, token, quote, battito) |
| `main/link/ble_protocol.h` | la forma scritta dal lato della scheda: è la fonte di verità del protocollo |

---

## 11. Provare in cinque minuti

1. La scheda deve essere accesa e non collegata a nessun altro client.
2. Nella cartella `web/`: `npm install` (la prima volta) e `npm run dev`.
3. Apri `http://localhost:5173` con **Chrome**.
4. Se compare *"Associazione non più valida"*, tieni premuto il pulsante di
   **BOOT per 5 secondi**: la scheda apre la finestra (schermata
   `COMMISSIONING` con il conto alla rovescia), poi premi **COMMISSIONA
   SCHEDA** e scegli `PADEL_SCORE_XXXX`.
5. Un click sul pulsante → la pagina mostra 15 a NOI e nella diagnostica
   compare `Ultimo evento: OUR_POINT`.
6. Una pressione di un secondo e mezzo, poi il rilascio → `Ultimo evento:
   MOMENT` e il punteggio non cambia: è il segno nel tempo, per ora solo
   annunciato.
