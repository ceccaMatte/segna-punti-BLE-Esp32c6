/**
 * Il collegamento con la scheda.
 *
 * Si occupa solo di Bluetooth: trova la scheda, apre il collegamento, legge e
 * scrive pacchetti e avvisa chi sta fuori quando succede qualcosa. Non sa
 * niente di schermate e non calcola niente del padel: i byte che riceve li
 * consegna gia' decodificati, e i byte che manda li riceve gia' pronti.
 *
 * Due cose che Web Bluetooth impone e che vale la pena di sapere:
 *
 *   - `requestDevice()` deve partire da un gesto dell'utente (un click): il
 *     browser mostra la sua finestra di scelta e non si puo' evitare;
 *   - il collegamento non sopravvive al ricaricamento della pagina, e
 *     ricollegarsi senza chiedere nulla e' possibile solo dove il browser
 *     permette `getDevices()`, che in Chrome sta ancora dietro un'impostazione
 *     sperimentale. Dove non c'e', si riparte dal pulsante di riconnessione.
 */

import {
  COMMISSIONING_CONTROL_UUID,
  COMMISSIONING_STATUS_UUID,
  DEVICE_INFO_UUID,
  NAME_PREFIX,
  ProtocolError,
  SCORE_STATE_UUID,
  SERVICE_UUID,
  ControlOp,
  decodeCommissioningStatus,
  decodeDeviceInfo,
  decodeScoreState,
  encodeControl,
  type CommissioningStatusPacket,
  type DeviceInfoPacket,
  type ScoreStatePacket,
} from './protocol';

export interface ClientEvents {
  onConnected?: (device: { name: string; id: string }) => void;
  onDisconnected?: () => void;
  onDeviceInfo?: (info: DeviceInfoPacket) => void;
  onStatus?: (status: CommissioningStatusPacket) => void;
  onScore?: (score: ScoreStatePacket) => void;
  onError?: (message: string) => void;
}

/** Una scheda che questa pagina ha il permesso di rivedere. */
export interface KnownDevice {
  id: string;
  name: string;
}

/** Vero se il browser ha Web Bluetooth. */
export function bluetoothAvailable(): boolean {
  return typeof navigator !== 'undefined' && 'bluetooth' in navigator;
}

/**
 * Le schede che il browser lascia rivedere a questa pagina.
 *
 * Non e' una scansione: e' l'elenco dei permessi, e contiene solo le schede
 * scelte almeno una volta da questa pagina. Non si possono elencare le schede
 * che passano li' intorno, e non e' una cosa che si possa aggirare: le regole
 * di Web Bluetooth non lo permettono.
 *
 * @return le schede note, oppure null se il browser non sa elencarle. Le due
 *         cose sono diverse: "non si puo' sapere" non e' "non ce n'e' nesuna".
 */
export async function listKnownDevices(): Promise<KnownDevice[] | null> {
  if (!bluetoothAvailable()) {
    return null;
  }

  const bluetooth = navigator.bluetooth as Bluetooth & {
    getDevices?: () => Promise<BluetoothDevice[]>;
  };

  if (typeof bluetooth.getDevices !== 'function') {
    return null;
  }

  try {
    const devices = await bluetooth.getDevices();
    return devices.map((device) => ({
      id: device.id,
      name: device.name ?? '(senza nome)',
    }));
  } catch {
    return null;
  }
}

export class PadelBleClient {
  private device: BluetoothDevice | null = null;
  private server: BluetoothRemoteGATTServer | null = null;
  private scoreCharacteristic: BluetoothRemoteGATTCharacteristic | null = null;
  private statusCharacteristic: BluetoothRemoteGATTCharacteristic | null = null;
  private controlCharacteristic: BluetoothRemoteGATTCharacteristic | null = null;
  private infoCharacteristic: BluetoothRemoteGATTCharacteristic | null = null;
  private readonly listeners: ClientEvents;

  /** La scheda gia' guardata, per non registrarne due volte la caduta. */
  private watched: BluetoothDevice | null = null;

  /** Vero se le notifiche sono gia' state chieste, per non chiederle due volte. */
  private scoreListening = false;
  private statusListening = false;

  constructor(listeners: ClientEvents = {}) {
    this.listeners = listeners;
  }

  get connected(): boolean {
    return this.server?.connected === true;
  }

  /**
   * Vero se la pagina ha ancora in mano la scheda scelta.
   *
   * E' quello che distingue "posso riprovare da sola" da "serve che l'utente
   * la scelga di nuovo": l'oggetto della scheda non sopravvive al
   * ricaricamento della pagina. Anche dopo uno SCOLLEGA resta in mano: si e'
   * chiuso il collegamento, non dimenticata la scheda.
   */
  get hasDevice(): boolean {
    return this.device !== null;
  }

  get deviceName(): string | null {
    return this.device?.name ?? null;
  }

  get deviceId(): string | null {
    return this.device?.id ?? null;
  }

  /**
   * Chiede al browser di far scegliere una scheda.
   *
   * Il filtro sul servizio e' quello che conta: il nome si puo' cambiare, il
   * servizio no. Il prefisso del nome si aggiunge solo come aiuto per l'occhio
   * nella finestra di scelta, e per non far comparire in elenco le cose che non
   * c'entrano.
   */
  async requestDevice(): Promise<BluetoothDevice> {
    const device = await navigator.bluetooth.requestDevice({
      filters: [{ services: [SERVICE_UUID] }, { namePrefix: NAME_PREFIX }],
      optionalServices: [SERVICE_UUID],
    });

    this.device = device;
    this.watch(device);

    return device;
  }

  /** Riprende una scheda gia' autorizzata dal browser, se il browser lo permette. */
  async findKnownDevice(deviceId: string): Promise<BluetoothDevice | null> {
    const bluetooth = navigator.bluetooth as Bluetooth & {
      getDevices?: () => Promise<BluetoothDevice[]>;
    };

    if (typeof bluetooth.getDevices !== 'function') {
      return null;
    }

    try {
      const devices = await bluetooth.getDevices();
      const found = devices.find((candidate) => candidate.id === deviceId) ?? null;
      if (found !== null) {
        this.device = found;
        this.watch(found);
      }
      return found;
    } catch {
      return null;
    }
  }

  /** Apre il collegamento e trova le characteristic. */
  async connect(device: BluetoothDevice | null = this.device): Promise<void> {
    if (device === null) {
      throw new Error('nessuna scheda scelta');
    }

    this.device = device;

    const server = await device.gatt?.connect();
    if (!server) {
      throw new Error('collegamento non riuscito');
    }
    this.server = server;

    const service = await server.getPrimaryService(SERVICE_UUID);

    const score = await service.getCharacteristic(SCORE_STATE_UUID);
    const control = await service.getCharacteristic(COMMISSIONING_CONTROL_UUID);
    const status = await service.getCharacteristic(COMMISSIONING_STATUS_UUID);
    const info = await service.getCharacteristic(DEVICE_INFO_UUID);

    this.scoreCharacteristic = score;
    this.statusCharacteristic = status;
    this.controlCharacteristic = control;
    this.infoCharacteristic = info;

    this.listeners.onConnected?.({
      name: device.name ?? '(senza nome)',
      id: device.id,
    });
  }

  async disconnect(): Promise<void> {
    this.server?.disconnect();
    this.forgetConnection();
  }

  /**
   * Tiene d'occhio una scheda.
   *
   * La caduta del collegamento puo' arrivare da sola — la scheda si riavvia, o
   * si allontana — e va raccontata una volta sola per scheda: registrandola a
   * ogni tentativo di riconnessione, la stessa caduta arriverebbe piu' volte, e
   * la pagina riproverebbe a collegarsi due volte per ogni disconnessione.
   */
  private watch(device: BluetoothDevice): void {
    if (this.watched === device) {
      return;
    }

    this.watched = device;
    device.addEventListener('gattserverdisconnected', () => {
      this.forgetConnection();
      this.listeners.onDisconnected?.();
    });
  }

  /** Butta via quello che vale solo finche' il collegamento e' aperto. */
  private forgetConnection(): void {
    this.server = null;
    this.scoreCharacteristic = null;
    this.statusCharacteristic = null;
    this.controlCharacteristic = null;
    this.infoCharacteristic = null;
    this.scoreListening = false;
    this.statusListening = false;
  }

  /* ------------------------------------------------------------------------ */
  /* Letture                                                                  */
  /* ------------------------------------------------------------------------ */

  async readDeviceInfo(): Promise<DeviceInfoPacket> {
    const value = await this.require(this.infoCharacteristic).readValue();
    return decodeDeviceInfo(value);
  }

  async readStatus(): Promise<CommissioningStatusPacket> {
    const value = await this.require(this.statusCharacteristic).readValue();
    return decodeCommissioningStatus(value);
  }

  async readScore(): Promise<ScoreStatePacket> {
    const value = await this.require(this.scoreCharacteristic).readValue();
    return decodeScoreState(value);
  }

  /* ------------------------------------------------------------------------ */
  /* Comandi                                                                  */
  /* ------------------------------------------------------------------------ */

  async claim(token: Uint8Array): Promise<void> {
    await this.writeControl(ControlOp.Claim, token);
  }

  async auth(token: Uint8Array): Promise<void> {
    await this.writeControl(ControlOp.Auth, token);
  }

  private async writeControl(op: ControlOp, token: Uint8Array): Promise<void> {
    const payload = encodeControl(op, token);
    /*
     * `writeValueWithResponse` e non `writeValue`: la scrittura con risposta
     * arriva solo quando la scheda ha davvero preso in carico il comando, e se
     * qualcosa va storto il browser lo dice. Per un'associazione, sapere che il
     * comando e' arrivato vale l'attesa di qualche millisecondo in piu'.
     */
    await this.require(this.controlCharacteristic).writeValueWithResponse(payload);
  }

  /* ------------------------------------------------------------------------ */
  /* Notifiche                                                                */
  /* ------------------------------------------------------------------------ */

  /** Si iscrive agli aggiornamenti della partita. */
  async subscribeScore(): Promise<void> {
    if (this.scoreListening) {
      return;
    }

    const characteristic = this.require(this.scoreCharacteristic);

    characteristic.addEventListener('characteristicvaluechanged', () => {
      const value = characteristic.value;
      if (!value) {
        return;
      }
      try {
        this.listeners.onScore?.(decodeScoreState(value));
      } catch (error) {
        this.report(error);
      }
    });

    this.scoreListening = true;
    await characteristic.startNotifications();
  }

  /** Si iscrive agli aggiornamenti dello stato dell'associazione. */
  async subscribeStatus(): Promise<void> {
    if (this.statusListening) {
      return;
    }

    const characteristic = this.require(this.statusCharacteristic);

    characteristic.addEventListener('characteristicvaluechanged', () => {
      const value = characteristic.value;
      if (!value) {
        return;
      }
      try {
        this.listeners.onStatus?.(decodeCommissioningStatus(value));
      } catch (error) {
        this.report(error);
      }
    });

    this.statusListening = true;
    await characteristic.startNotifications();
  }

  /* ------------------------------------------------------------------------ */

  private require(characteristic: BluetoothRemoteGATTCharacteristic | null): BluetoothRemoteGATTCharacteristic {
    if (characteristic === null) {
      throw new Error('collegamento non aperto');
    }
    return characteristic;
  }

  private report(error: unknown): void {
    const message =
      error instanceof ProtocolError || error instanceof Error
        ? error.message
        : 'errore sconosciuto';
    this.listeners.onError?.(message);
  }
}
