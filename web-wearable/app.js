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
const PRIMITIVES = ['Click', 'Doppio click', 'Triplo click', 'Pressione lunga'];
const RETRY_MS = [500, 500, 1000, 1000, 2000, 2000, 3000, 5000, 8000];

let device = null;
let chars = {};
let config = null;
let reconnectAttempt = 0;
let reconnectTimer = null;
let suppressReconnect = false;
const ackCache = new Map();

const $ = id => document.getElementById(id);
const status = (text, ok = true) => {
  $('status').textContent = text;
  $('status').className = ok ? 'ok' : 'bad';
};

function u16(dv, o) { return dv.getUint16(o, true); }
function put16(dv, o, v) { dv.setUint16(o, Number(v), true); }

function token(button, primitive) {
  return ((button & 3) << 2) | (primitive & 3);
}
function tokenButton(t) { return (t >> 2) & 3; }
function tokenPrimitive(t) { return t & 3; }

function randomToken() {
  const t = new Uint8Array(16);
  crypto.getRandomValues(t);
  return t;
}
function tokenKey() { return device ? `playmaker-token:${device.id}` : ''; }
function loadToken() {
  const s = localStorage.getItem(tokenKey());
  if (!s) return null;
  const a = s.split(',').map(Number);
  return a.length === 16 ? new Uint8Array(a) : null;
}
function saveToken(t) {
  localStorage.setItem(tokenKey(), Array.from(t).join(','));
}

async function writeChar(ch, bytes) {
  if (ch.writeValueWithResponse) return ch.writeValueWithResponse(bytes);
  return ch.writeValue(bytes);
}

function controlPacket(op, tokenBytes) {
  const out = new Uint8Array(17);
  out[0] = op;
  out.set(tokenBytes, 1);
  return out;
}

function decodeStatus(dv) {
  if (dv.byteLength < 10 || dv.getUint8(0) !== 2 || dv.getUint8(1) !== 3) {
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
  return decodeStatus(await chars.status.readValue());
}

function onStatusNotification(ev) {
  try {
    const s = decodeStatus(ev.target.value);
    if (!s.commissioned && s.pairingOpen) {
      suppressReconnect = true;
      clearTimeout(reconnectTimer);
      reconnectTimer = null;
      if (device) localStorage.removeItem(tokenKey());
      status('Pairing aperto: riconnessione automatica sospesa');
    }
  } catch (e) {
    console.error('Status notification failed', e);
  }
}

async function authenticateOrClaim(allowClaim) {
  const s = await readStatus();
  let t = loadToken();

  if (s.commissioned) {
    if (!t) {
      suppressReconnect = true;
      if (device?.gatt?.connected) device.gatt.disconnect();
      throw new Error('Questo wearable è già associato. Avvia la gesture di pairing per sostituire l’associazione.');
    }
    try {
      await writeChar(chars.control, controlPacket(0x02, t));
    } catch (e) {
      suppressReconnect = true;
      if (device?.gatt?.connected) device.gatt.disconnect();
      throw new Error('Token di associazione rifiutato: avvia il pairing fisico sul wearable.');
    }
  } else {
    if (!s.pairingOpen) {
      throw new Error('Finestra di pairing chiusa. Esegui la gesture di pairing sul wearable.');
    }
    if (!allowClaim) {
      suppressReconnect = true;
      if (device?.gatt?.connected) device.gatt.disconnect();
      throw new Error('Wearable in pairing: usa Connetti / Pair per autorizzare una nuova associazione.');
    }
    if (!t) {
      t = randomToken();
      saveToken(t);
    }
    await writeChar(chars.control, controlPacket(0x01, t));
  }

  const after = await readStatus();
  if (!after.authenticated) throw new Error('Autenticazione BLE applicativa fallita.');
  suppressReconnect = false;
}

async function connectDevice(d, { allowClaim = false } = {}) {
  clearTimeout(reconnectTimer);
  device = d;
  device.removeEventListener('gattserverdisconnected', onDisconnected);
  device.addEventListener('gattserverdisconnected', onDisconnected);

  status('Connessione…');
  const server = await device.gatt.connect();
  const svc = await server.getPrimaryService(SERVICE);

  chars = {
    action: await svc.getCharacteristic(UUID.action),
    ack: await svc.getCharacteristic(UUID.ack),
    control: await svc.getCharacteristic(UUID.control),
    status: await svc.getCharacteristic(UUID.status),
    config: await svc.getCharacteristic(UUID.config),
  };

  await chars.status.startNotifications();
  chars.status.addEventListener('characteristicvaluechanged', onStatusNotification);
  await chars.action.startNotifications();
  chars.action.addEventListener('characteristicvaluechanged', onAction);

  await authenticateOrClaim(allowClaim);
  reconnectAttempt = 0;
  status(`Connesso a ${device.name}`);
  await refreshConfig();
}

async function requestConnect() {
  suppressReconnect = false;
  const d = await navigator.bluetooth.requestDevice({
    filters: [{ services: [SERVICE] }],
  });
  await connectDevice(d, { allowClaim: true });
}

async function findGrantedDevice() {
  if (!navigator.bluetooth.getDevices) return null;
  const devices = await navigator.bluetooth.getDevices();
  return devices.find(d => d.name?.startsWith('PLAYMAKER_')) ?? null;
}

async function autoReconnect() {
  if (suppressReconnect) return false;
  try {
    const d = device ?? await findGrantedDevice();
    if (!d) {
      status('Nessun wearable già autorizzato dal browser', false);
      return false;
    }
    await connectDevice(d, { allowClaim: false });
    return true;
  } catch (e) {
    status(e.message || String(e), false);
    return false;
  }
}

function onDisconnected() {
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
  reconnectTimer = setTimeout(async () => {
    if (!(await autoReconnect())) planReconnect();
  }, delay);
}

function ackForAction(dv) {
  const session = dv.getUint32(2, true);
  const sequence = dv.getUint32(6, true);
  const key = `${session}:${sequence}`;
  const cached = ackCache.get(key);
  if (cached) return cached;

  // Questa è solo la pagina di configurazione/collaudo: non possiede il
  // motore partita. L'app Playmaker reale deve costruire questo ACK usando lo
  // stato AUTOREVOLE dopo aver applicato il comando e deve mettere i flag
  // GAME_ENDED / SET_ENDED / MATCH_ENDED causati da questo specifico comando.
  //
  // Layout ACK v2 (20 byte):
  // [ver,type,status,transitions, session:u32, seq:u32, revision:u16,
  //  pointsA,pointsB,gamesA,gamesB,setsA,setsB]
  const out = new Uint8Array(20);
  const a = new DataView(out.buffer);
  a.setUint8(0, 2);              // protocol version
  a.setUint8(1, 2);              // ACK
  a.setUint8(2, 0);              // status = OK
  a.setUint8(3, 0);              // transition flags
  a.setUint32(4, session, true);
  a.setUint32(8, sequence, true);
  a.setUint16(12, 0, true);      // match revision (mock)
  // 14..19 = authoritative score snapshot; zero in this test page.
  ackCache.set(key, out);

  if (ackCache.size > 64) {
    ackCache.delete(ackCache.keys().next().value);
  }
  return out;
}

async function onAction(ev) {
  try {
    const dv = ev.target.value;
    if (dv.byteLength < 12 || dv.getUint8(0) !== 2 || dv.getUint8(1) !== 1) return;
    await writeChar(chars.ack, ackForAction(dv));
  } catch (e) {
    console.error('ACK failed', e);
  }
}

function parseConfig(dv) {
  if (dv.byteLength < 9 || dv.getUint8(0) !== 2 || dv.getUint8(1) !== 4) {
    throw new Error('Config protocol mismatch');
  }
  const count = dv.getUint8(2);
  if (count > 16) throw new Error('Numero gesture non valido');
  const wanted = 9 + count * 10 + 4 * 6;
  if (dv.byteLength < wanted) throw new Error('Config packet truncated');
  const cfg = {
    mappingCount: count,
    multiGap: u16(dv, 3),
    longMs: u16(dv, 5),
    seqGap: u16(dv, 7),
    mappings: [],
    sounds: [],
  };

  let o = 9;
  for (let i = 0; i < count; i++) {
    const action = dv.getUint8(o++);
    const len = dv.getUint8(o++);
    const seq = [];
    for (let t = 0; t < 8; t++) {
      const v = dv.getUint8(o++);
      if (t < len) seq.push(v);
    }
    cfg.mappings.push({ action, seq });
  }

  for (let i = 0; i < 4; i++) {
    cfg.sounds.push({
      frequency: u16(dv, o),
      duty: u16(dv, o + 2),
      duration: u16(dv, o + 4),
    });
    o += 6;
  }
  return cfg;
}

async function refreshConfig() {
  const dv = await chars.config.readValue();
  config = parseConfig(dv);
  render();
}

function option(value, text, selected) {
  return `<option value="${value}" ${selected ? 'selected' : ''}>${text}</option>`;
}

function renderStep(t, idx, stepIdx) {
  const b = tokenButton(t);
  const p = tokenPrimitive(t);
  return `
    <span class="step">
      <select data-map="${idx}" data-step="${stepIdx}" data-kind="button">
        ${BUTTON_NAMES.map((name, x) => option(x, name, x === b)).join('')}
      </select>
      <select data-map="${idx}" data-step="${stepIdx}" data-kind="primitive">
        ${PRIMITIVES.map((x, i) => option(i, x, i === p)).join('')}
      </select>
      <button data-remove-step="${idx}:${stepIdx}">×</button>
    </span>`;
}

function render() {
  if (!config) return;

  $('seqGap').value = config.seqGap;
  $('multiGap').value = config.multiGap;
  $('longMs').value = config.longMs;

  $('mappings').innerHTML = config.mappings.map((m, idx) => `
    <div class="mapping" data-row="${idx}">
      <div class="row">
        <strong>#${idx + 1}</strong>
        <select data-action="${idx}">
          ${ACTIONS.map((a, i) => option(i, a, i === m.action)).join('')}
        </select>
        <span id="steps-${idx}">
          ${m.seq.map((t, s) => renderStep(t, idx, s)).join('')}
        </span>
        <button data-add-step="${idx}">+ step</button>
        <button data-save-map="${idx}">Salva</button>
        <button data-delete-map="${idx}">Elimina</button>
      </div>
    </div>
  `).join('');

  $('sounds').innerHTML = config.sounds.map((s, idx) => `
    <div class="row">
      <strong>${ACTIONS[idx]}</strong>
      <label>Hz <input data-sound="${idx}" data-field="frequency" type="number" min="100" max="8000" value="${s.frequency}"></label>
      <label>Duty ‰ <input data-sound="${idx}" data-field="duty" type="number" min="0" max="900" value="${s.duty}"></label>
      <label>Durata ms <input data-sound="${idx}" data-field="duration" type="number" min="1" max="2000" value="${s.duration}"></label>
      <button data-save-sound="${idx}">Salva suono</button>
    </div>
  `).join('');
}

function syncMappingFromDom(idx) {
  const actionEl = document.querySelector(`[data-action="${idx}"]`);
  config.mappings[idx].action = Number(actionEl.value);

  const buttons = [...document.querySelectorAll(`[data-map="${idx}"][data-kind="button"]`)];
  config.mappings[idx].seq = buttons.map((b, stepIdx) => {
    const p = document.querySelector(`[data-map="${idx}"][data-step="${stepIdx}"][data-kind="primitive"]`);
    return token(Number(b.value), Number(p.value));
  });
}

async function saveMapping(idx) {
  syncMappingFromDom(idx);
  const m = config.mappings[idx];
  if (m.seq.length < 1 || m.seq.length > 8) throw new Error('Una gesture deve avere da 1 a 8 step');

  const cmd = new Uint8Array(12);
  cmd[0] = 0x10;
  cmd[1] = idx;
  cmd[2] = m.action;
  cmd[3] = m.seq.length;
  cmd.set(m.seq, 4);
  await writeChar(chars.config, cmd);
  await refreshConfig();
}

async function deleteMapping(idx) {
  await writeChar(chars.config, new Uint8Array([0x11, idx]));
  await refreshConfig();
}

async function saveSound(idx) {
  const values = {};
  document.querySelectorAll(`[data-sound="${idx}"]`).forEach(el => values[el.dataset.field] = Number(el.value));
  const out = new Uint8Array(8);
  const dv = new DataView(out.buffer);
  out[0] = 0x12;
  out[1] = idx;
  put16(dv, 2, values.frequency);
  put16(dv, 4, values.duty);
  put16(dv, 6, values.duration);
  await writeChar(chars.config, out);
  await refreshConfig();
}

$('connect').onclick = async () => {
  try { await requestConnect(); }
  catch (e) { status(e.message || String(e), false); }
};
$('auto').onclick = () => {
  suppressReconnect = false;
  void autoReconnect();
};

$('saveTiming').onclick = async () => {
  try {
    const out = new Uint8Array(7);
    const dv = new DataView(out.buffer);
    out[0] = 0x13;
    put16(dv, 1, $('seqGap').value);
    put16(dv, 3, $('multiGap').value);
    put16(dv, 5, $('longMs').value);
    await writeChar(chars.config, out);
    await refreshConfig();
  } catch (e) { status(e.message || String(e), false); }
};

$('addMapping').onclick = () => {
  if (!config || config.mappings.length >= 16) return;
  config.mappings.push({ action: 0, seq: [token(0, 1)] });
  render();
};

$('defaults').onclick = async () => {
  try {
    await writeChar(chars.config, new Uint8Array([0x14]));
    await refreshConfig();
  } catch (e) { status(e.message || String(e), false); }
};

document.body.addEventListener('click', async ev => {
  const el = ev.target;
  try {
    if (el.dataset.addStep !== undefined) {
      const idx = Number(el.dataset.addStep);
      syncMappingFromDom(idx);
      if (config.mappings[idx].seq.length < 8) config.mappings[idx].seq.push(token(0, 0));
      render();
    }
    if (el.dataset.removeStep !== undefined) {
      const [idx, step] = el.dataset.removeStep.split(':').map(Number);
      syncMappingFromDom(idx);
      config.mappings[idx].seq.splice(step, 1);
      render();
    }
    if (el.dataset.saveMap !== undefined) await saveMapping(Number(el.dataset.saveMap));
    if (el.dataset.deleteMap !== undefined) await deleteMapping(Number(el.dataset.deleteMap));
    if (el.dataset.saveSound !== undefined) await saveSound(Number(el.dataset.saveSound));
  } catch (e) {
    status(e.message || String(e), false);
  }
});

window.addEventListener('load', () => {
  if (!navigator.bluetooth) {
    status('Web Bluetooth non disponibile in questo browser', false);
  }
});
