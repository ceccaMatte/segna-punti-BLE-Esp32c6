const PROTOCOL_VERSION = 3;
const SERVICE = 'c4d10001-6f65-4b6e-ae30-706c61796d6b';
const UUID = {
  action:  'c4d10002-6f65-4b6e-ae30-706c61796d6b',
  ack:     'c4d10003-6f65-4b6e-ae30-706c61796d6b',
  control: 'c4d10004-6f65-4b6e-ae30-706c61796d6b',
  status:  'c4d10005-6f65-4b6e-ae30-706c61796d6b',
  config:  'c4d10006-6f65-4b6e-ae30-706c61796d6b',
};

const ACTIONS = ['Point A', 'Point B', 'Moment', 'Undo', 'Pairing'];
const BUTTON_NAMES = ['A', 'B', 'Moment', 'Undo'];
const PRIMITIVES = ['Click', 'Doppio click', 'Triplo click', 'Pressione lunga', 'Simultanea'];
const PRIMITIVE_CHORD = 4;
const RETRY_MS = [500, 500, 1000, 1000, 2000, 2000, 3000, 5000, 8000];
const MAX_SEQUENCE = 8;
const MAX_MAPPINGS = 16;
const CONFIG_HEADER_SIZE = 11;
const CONFIG_MAPPING_SIZE = 18;

let device = null;
let chars = {};
let config = null;
let reconnectAttempt = 0;
let reconnectTimer = null;
let suppressReconnect = false;
const ackCache = new Map();

const $ = id => document.getElementById(id);

function diag(event, data = undefined) {
  const prefix = '[Playmaker][BLE]';
  if (data === undefined) console.debug(prefix, event);
  else console.debug(prefix, event, data);
}

function diagError(event, error) {
  console.error('[Playmaker][BLE]', event, error);
}

function status(text, ok = true) {
  $('status').textContent = text;
  $('status').className = ok ? 'ok' : 'bad';
}

function u16(dv, offset) {
  return dv.getUint16(offset, true);
}

function put16(dv, offset, value) {
  dv.setUint16(offset, Number(value), true);
}

function token(buttonMask, primitive) {
  return ((primitive & 0x07) << 4) | (buttonMask & 0x0f);
}

function singleToken(button, primitive) {
  return token(1 << button, primitive);
}

function tokenMask(value) {
  return value & 0x0f;
}

function tokenPrimitive(value) {
  return (value >> 4) & 0x07;
}

function popcount4(mask) {
  mask &= 0x0f;
  return (mask & 1) + ((mask >> 1) & 1) + ((mask >> 2) & 1) + ((mask >> 3) & 1);
}

function firstButton(mask) {
  for (let i = 0; i < BUTTON_NAMES.length; i++) {
    if (mask & (1 << i)) return i;
  }
  return 0;
}

function normalizeTokenForPrimitive(value, primitive) {
  let mask = tokenMask(value);

  if (primitive === PRIMITIVE_CHORD) {
    if (popcount4(mask) < 2) {
      const first = firstButton(mask || 1);
      const second = first === 0 ? 1 : 0;
      mask = (1 << first) | (1 << second);
    }
  } else {
    mask = 1 << firstButton(mask || 1);
  }

  return token(mask, primitive);
}

function randomToken() {
  const result = new Uint8Array(16);
  crypto.getRandomValues(result);
  return result;
}

function tokenKey() {
  return device ? `playmaker-token:${device.id}` : '';
}

function loadToken() {
  const raw = localStorage.getItem(tokenKey());
  if (!raw) return null;

  const values = raw.split(',').map(Number);
  return values.length === 16 ? new Uint8Array(values) : null;
}

function saveToken(value) {
  localStorage.setItem(tokenKey(), Array.from(value).join(','));
}

async function writeChar(characteristic, bytes) {
  diag('GATT write', { uuid: characteristic.uuid, bytes: Array.from(bytes) });
  if (characteristic.writeValueWithResponse) {
    return characteristic.writeValueWithResponse(bytes);
  }
  return characteristic.writeValue(bytes);
}

function controlPacket(op, tokenBytes) {
  const out = new Uint8Array(17);
  out[0] = op;
  out.set(tokenBytes, 1);
  return out;
}

function decodeStatus(dv) {
  if (dv.byteLength < 10 ||
      dv.getUint8(0) !== PROTOCOL_VERSION ||
      dv.getUint8(1) !== 3) {
    throw new Error('Status protocol mismatch');
  }

  return {
    commissioned: dv.getUint8(2) !== 0,
    authenticated: dv.getUint8(3) !== 0,
    pairingOpen: dv.getUint8(4) !== 0,
    batteryMv: dv.getUint16(5, true),
  };
}

async function readStatus() {
  const result = decodeStatus(await chars.status.readValue());
  diag('status read', result);
  return result;
}

function onStatusNotification(ev) {
  try {
    const value = decodeStatus(ev.target.value);
    diag('status notify', value);

    if (!value.commissioned && value.pairingOpen) {
      suppressReconnect = true;
      clearTimeout(reconnectTimer);
      reconnectTimer = null;
      if (device) localStorage.removeItem(tokenKey());
      status('Pairing aperto: riconnessione automatica sospesa');
    }
  } catch (error) {
    diagError('status notification failed', error);
  }
}

async function authenticateOrClaim(allowClaim) {
  const current = await readStatus();
  let pairingToken = loadToken();

  if (current.commissioned) {
    if (!pairingToken) {
      suppressReconnect = true;
      if (device?.gatt?.connected) device.gatt.disconnect();
      throw new Error('Questo wearable è già associato. Avvia la gesture di pairing per sostituire l’associazione.');
    }

    diag('authenticate existing association');
    try {
      await writeChar(chars.control, controlPacket(0x02, pairingToken));
    } catch (error) {
      suppressReconnect = true;
      if (device?.gatt?.connected) device.gatt.disconnect();
      throw new Error('Token di associazione rifiutato: avvia il pairing fisico sul wearable.');
    }
  } else {
    if (!current.pairingOpen) {
      throw new Error('Finestra di pairing chiusa. Esegui la gesture di pairing sul wearable.');
    }

    if (!allowClaim) {
      suppressReconnect = true;
      if (device?.gatt?.connected) device.gatt.disconnect();
      throw new Error('Wearable in pairing: usa Connetti / Pair per autorizzare una nuova associazione.');
    }

    if (!pairingToken) {
      pairingToken = randomToken();
      saveToken(pairingToken);
    }

    diag('claim new association');
    await writeChar(chars.control, controlPacket(0x01, pairingToken));
  }

  const after = await readStatus();
  if (!after.authenticated) {
    throw new Error('Autenticazione BLE applicativa fallita.');
  }

  suppressReconnect = false;
  diag('authenticated', after);
}

async function connectDevice(target, { allowClaim = false } = {}) {
  clearTimeout(reconnectTimer);
  device = target;

  device.removeEventListener('gattserverdisconnected', onDisconnected);
  device.addEventListener('gattserverdisconnected', onDisconnected);

  status('Connessione…');
  diag('connect begin', { id: device.id, name: device.name, allowClaim });

  const server = await device.gatt.connect();
  const service = await server.getPrimaryService(SERVICE);

  chars = {
    action: await service.getCharacteristic(UUID.action),
    ack: await service.getCharacteristic(UUID.ack),
    control: await service.getCharacteristic(UUID.control),
    status: await service.getCharacteristic(UUID.status),
    config: await service.getCharacteristic(UUID.config),
  };

  await chars.status.startNotifications();
  chars.status.removeEventListener('characteristicvaluechanged', onStatusNotification);
  chars.status.addEventListener('characteristicvaluechanged', onStatusNotification);

  await chars.action.startNotifications();
  chars.action.removeEventListener('characteristicvaluechanged', onAction);
  chars.action.addEventListener('characteristicvaluechanged', onAction);

  await authenticateOrClaim(allowClaim);

  reconnectAttempt = 0;
  status(`Connesso a ${device.name}`);
  diag('connect ready', { name: device.name });
  await refreshConfig();
}

async function requestConnect() {
  suppressReconnect = false;

  const target = await navigator.bluetooth.requestDevice({
    filters: [{ services: [SERVICE] }],
  });

  await connectDevice(target, { allowClaim: true });
}

async function findGrantedDevice() {
  if (!navigator.bluetooth.getDevices) return null;

  const devices = await navigator.bluetooth.getDevices();
  return devices.find(d => d.name?.startsWith('PLAYMAKER_')) ?? null;
}

async function autoReconnect() {
  if (suppressReconnect) return false;

  try {
    const target = device ?? await findGrantedDevice();
    if (!target) {
      status('Nessun wearable già autorizzato dal browser', false);
      return false;
    }

    await connectDevice(target, { allowClaim: false });
    return true;
  } catch (error) {
    diagError('auto reconnect failed', error);
    status(error.message || String(error), false);
    return false;
  }
}

function onDisconnected() {
  diag('disconnected');

  if (suppressReconnect) {
    status('Disconnesso per pairing; riconnessione automatica sospesa');
    return;
  }

  status('Disconnesso, riconnessione automatica…', false);
  planReconnect();
}

function planReconnect() {
  if (suppressReconnect) return;

  clearTimeout(reconnectTimer);
  const delay = RETRY_MS[Math.min(reconnectAttempt, RETRY_MS.length - 1)];
  reconnectAttempt++;

  diag('reconnect scheduled', { delay, attempt: reconnectAttempt });

  reconnectTimer = setTimeout(async () => {
    if (!(await autoReconnect())) planReconnect();
  }, delay);
}

function ackForAction(dv) {
  const session = dv.getUint32(2, true);
  const sequence = dv.getUint32(6, true);
  const action = dv.getUint8(10);
  const key = `${session}:${sequence}`;

  const cached = ackCache.get(key);
  if (cached) {
    diag('duplicate action -> cached ACK', { session, sequence, action });
    return cached;
  }

  /*
   * This configuration page is only a transport test client. The real
   * Playmaker app must fill the authoritative state and transition flags.
   */
  const out = new Uint8Array(20);
  const ack = new DataView(out.buffer);
  ack.setUint8(0, PROTOCOL_VERSION);
  ack.setUint8(1, 2);
  ack.setUint8(2, 0);
  ack.setUint8(3, 0);
  ack.setUint32(4, session, true);
  ack.setUint32(8, sequence, true);
  ack.setUint16(12, 0, true);

  ackCache.set(key, out);
  if (ackCache.size > 64) {
    ackCache.delete(ackCache.keys().next().value);
  }

  return out;
}

async function onAction(ev) {
  try {
    const dv = ev.target.value;

    if (dv.byteLength < 12 ||
        dv.getUint8(0) !== PROTOCOL_VERSION ||
        dv.getUint8(1) !== 1) {
      diag('discard invalid ACTION');
      return;
    }

    const action = {
      session: dv.getUint32(2, true),
      sequence: dv.getUint32(6, true),
      action: dv.getUint8(10),
    };
    diag('ACTION rx', action);

    const ack = ackForAction(dv);
    await writeChar(chars.ack, ack);
    diag('ACK tx', {
      session: action.session,
      sequence: action.sequence,
      status: ack[2],
      flags: ack[3],
    });
  } catch (error) {
    diagError('ACK failed', error);
  }
}

function parseConfig(dv) {
  if (dv.byteLength < CONFIG_HEADER_SIZE ||
      dv.getUint8(0) !== PROTOCOL_VERSION ||
      dv.getUint8(1) !== 4) {
    throw new Error('Config protocol mismatch');
  }

  const count = dv.getUint8(2);
  if (count > MAX_MAPPINGS) {
    throw new Error('Numero gesture non valido');
  }

  const wanted = CONFIG_HEADER_SIZE +
                 count * CONFIG_MAPPING_SIZE +
                 4 * 6;
  if (dv.byteLength < wanted) {
    throw new Error('Config packet truncated');
  }

  const result = {
    mappingCount: count,
    multiGap: u16(dv, 3),
    longMs: u16(dv, 5),
    seqGap: u16(dv, 7),
    chordMs: u16(dv, 9),
    mappings: [],
    sounds: [],
  };

  let offset = CONFIG_HEADER_SIZE;

  for (let i = 0; i < count; i++) {
    const action = dv.getUint8(offset++);
    const length = dv.getUint8(offset++);
    const sequence = [];

    for (let t = 0; t < MAX_SEQUENCE; t++) {
      const value = u16(dv, offset);
      offset += 2;
      if (t < length) sequence.push(value);
    }

    result.mappings.push({ action, seq: sequence });
  }

  for (let i = 0; i < 4; i++) {
    result.sounds.push({
      frequency: u16(dv, offset),
      duty: u16(dv, offset + 2),
      duration: u16(dv, offset + 4),
    });
    offset += 6;
  }

  return result;
}

async function refreshConfig() {
  const dv = await chars.config.readValue();
  config = parseConfig(dv);
  diag('config read', config);
  render();
}

function option(value, text, selected) {
  return `<option value="${value}" ${selected ? 'selected' : ''}>${text}</option>`;
}

function renderButtonSelector(mask, mapIndex, stepIndex) {
  const button = firstButton(mask);
  return `
    <select data-map="${mapIndex}" data-step="${stepIndex}" data-kind="button">
      ${BUTTON_NAMES.map((name, index) => option(index, name, index === button)).join('')}
    </select>`;
}

function renderChordSelector(mask, mapIndex, stepIndex) {
  return `
    <span class="chord">
      ${BUTTON_NAMES.map((name, index) => `
        <label>
          <input
            type="checkbox"
            data-map="${mapIndex}"
            data-step="${stepIndex}"
            data-kind="chord-button"
            data-button="${index}"
            ${mask & (1 << index) ? 'checked' : ''}>
          ${name}
        </label>`).join('')}
    </span>`;
}

function renderStep(value, mapIndex, stepIndex) {
  const primitive = tokenPrimitive(value);
  const mask = tokenMask(value);

  return `
    <span class="step" data-step-container="${mapIndex}:${stepIndex}">
      <select data-map="${mapIndex}" data-step="${stepIndex}" data-kind="primitive">
        ${PRIMITIVES.map((name, index) => option(index, name, index === primitive)).join('')}
      </select>
      ${primitive === PRIMITIVE_CHORD
        ? renderChordSelector(mask, mapIndex, stepIndex)
        : renderButtonSelector(mask, mapIndex, stepIndex)}
      <button data-remove-step="${mapIndex}:${stepIndex}">×</button>
    </span>`;
}

function render() {
  if (!config) return;

  $('seqGap').value = config.seqGap;
  $('multiGap').value = config.multiGap;
  $('longMs').value = config.longMs;
  $('chordMs').value = config.chordMs;

  $('mappings').innerHTML = config.mappings.map((mapping, index) => `
    <div class="mapping" data-row="${index}">
      <div class="row">
        <strong>#${index + 1}</strong>
        <select data-action="${index}">
          ${ACTIONS.map((name, action) => option(action, name, action === mapping.action)).join('')}
        </select>
        <span>
          ${mapping.seq.map((value, step) => renderStep(value, index, step)).join('')}
        </span>
        <button data-add-step="${index}">+ step</button>
        <button data-save-map="${index}">Salva</button>
        <button data-delete-map="${index}">Elimina</button>
      </div>
    </div>
  `).join('');

  $('sounds').innerHTML = config.sounds.map((sound, index) => `
    <div class="row">
      <strong>${ACTIONS[index]}</strong>
      <label>Hz <input data-sound="${index}" data-field="frequency" type="number" min="100" max="8000" value="${sound.frequency}"></label>
      <label>Duty ‰ <input data-sound="${index}" data-field="duty" type="number" min="0" max="900" value="${sound.duty}"></label>
      <label>Durata ms <input data-sound="${index}" data-field="duration" type="number" min="1" max="2000" value="${sound.duration}"></label>
      <button data-save-sound="${index}">Salva suono</button>
    </div>
  `).join('');
}

function readStepFromDom(mapIndex, stepIndex) {
  const primitiveElement = document.querySelector(
    `[data-map="${mapIndex}"][data-step="${stepIndex}"][data-kind="primitive"]`);
  const primitive = Number(primitiveElement.value);

  if (primitive === PRIMITIVE_CHORD) {
    let mask = 0;
    document.querySelectorAll(
      `[data-map="${mapIndex}"][data-step="${stepIndex}"][data-kind="chord-button"]`
    ).forEach(input => {
      if (input.checked) mask |= 1 << Number(input.dataset.button);
    });

    if (popcount4(mask) < 2) {
      throw new Error('Una gesture simultanea deve contenere almeno due pulsanti.');
    }

    return token(mask, primitive);
  }

  const buttonElement = document.querySelector(
    `[data-map="${mapIndex}"][data-step="${stepIndex}"][data-kind="button"]`);
  return singleToken(Number(buttonElement.value), primitive);
}

function syncMappingFromDom(index) {
  const actionElement = document.querySelector(`[data-action="${index}"]`);
  config.mappings[index].action = Number(actionElement.value);

  config.mappings[index].seq = config.mappings[index].seq.map(
    (_, stepIndex) => readStepFromDom(index, stepIndex));
}

async function saveMapping(index) {
  syncMappingFromDom(index);
  const mapping = config.mappings[index];

  if (mapping.seq.length < 1 || mapping.seq.length > MAX_SEQUENCE) {
    throw new Error('Una gesture deve avere da 1 a 8 step.');
  }

  const command = new Uint8Array(4 + 2 * MAX_SEQUENCE);
  const dv = new DataView(command.buffer);
  command[0] = 0x10;
  command[1] = index;
  command[2] = mapping.action;
  command[3] = mapping.seq.length;

  mapping.seq.forEach((value, step) => put16(dv, 4 + step * 2, value));

  diag('save mapping', { index, mapping });
  await writeChar(chars.config, command);
  await refreshConfig();
}

async function deleteMapping(index) {
  diag('delete mapping', { index });
  await writeChar(chars.config, new Uint8Array([0x11, index]));
  await refreshConfig();
}

async function saveSound(index) {
  const values = {};
  document.querySelectorAll(`[data-sound="${index}"]`)
    .forEach(element => values[element.dataset.field] = Number(element.value));

  const out = new Uint8Array(8);
  const dv = new DataView(out.buffer);
  out[0] = 0x12;
  out[1] = index;
  put16(dv, 2, values.frequency);
  put16(dv, 4, values.duty);
  put16(dv, 6, values.duration);

  diag('save sound', { index, ...values });
  await writeChar(chars.config, out);
  await refreshConfig();
}

$('connect').onclick = async () => {
  try {
    await requestConnect();
  } catch (error) {
    diagError('manual connect failed', error);
    status(error.message || String(error), false);
  }
};

$('auto').onclick = () => {
  suppressReconnect = false;
  void autoReconnect();
};

$('saveTiming').onclick = async () => {
  try {
    const out = new Uint8Array(9);
    const dv = new DataView(out.buffer);
    out[0] = 0x13;
    put16(dv, 1, $('seqGap').value);
    put16(dv, 3, $('multiGap').value);
    put16(dv, 5, $('longMs').value);
    put16(dv, 7, $('chordMs').value);

    diag('save timings', {
      sequence: Number($('seqGap').value),
      multi: Number($('multiGap').value),
      long: Number($('longMs').value),
      chord: Number($('chordMs').value),
    });

    await writeChar(chars.config, out);
    await refreshConfig();
  } catch (error) {
    diagError('save timings failed', error);
    status(error.message || String(error), false);
  }
};

$('addMapping').onclick = () => {
  if (!config || config.mappings.length >= MAX_MAPPINGS) return;
  config.mappings.push({
    action: 0,
    seq: [singleToken(0, 0)],
  });
  render();
};

$('defaults').onclick = async () => {
  try {
    diag('reset defaults');
    await writeChar(chars.config, new Uint8Array([0x14]));
    await refreshConfig();
  } catch (error) {
    diagError('reset defaults failed', error);
    status(error.message || String(error), false);
  }
};

document.body.addEventListener('change', event => {
  const element = event.target;
  if (element.dataset.kind !== 'primitive') return;

  const mapIndex = Number(element.dataset.map);
  const stepIndex = Number(element.dataset.step);

  try {
    syncMappingFromDom(mapIndex);
  } catch {
    // The old DOM may contain a temporarily invalid chord selection.
  }

  const primitive = Number(element.value);
  const oldValue = config.mappings[mapIndex].seq[stepIndex];
  config.mappings[mapIndex].seq[stepIndex] =
    normalizeTokenForPrimitive(oldValue, primitive);
  render();
});

document.body.addEventListener('click', async event => {
  const element = event.target;

  try {
    if (element.dataset.addStep !== undefined) {
      const index = Number(element.dataset.addStep);
      syncMappingFromDom(index);

      if (config.mappings[index].seq.length < MAX_SEQUENCE) {
        config.mappings[index].seq.push(singleToken(0, 0));
      }
      render();
    }

    if (element.dataset.removeStep !== undefined) {
      const [index, step] =
        element.dataset.removeStep.split(':').map(Number);
      syncMappingFromDom(index);
      config.mappings[index].seq.splice(step, 1);
      render();
    }

    if (element.dataset.saveMap !== undefined) {
      await saveMapping(Number(element.dataset.saveMap));
    }

    if (element.dataset.deleteMap !== undefined) {
      await deleteMapping(Number(element.dataset.deleteMap));
    }

    if (element.dataset.saveSound !== undefined) {
      await saveSound(Number(element.dataset.saveSound));
    }
  } catch (error) {
    diagError('UI operation failed', error);
    status(error.message || String(error), false);
  }
});

window.addEventListener('load', () => {
  diag('page loaded', { protocol: PROTOCOL_VERSION });

  if (!navigator.bluetooth) {
    status('Web Bluetooth non disponibile in questo browser', false);
  }
});
