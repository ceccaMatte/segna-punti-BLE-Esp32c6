const PROTOCOL_VERSION = 3;
const ACTIONS = ['Point A', 'Point B', 'Moment', 'Undo', 'Pairing'];
const BUTTON_NAMES = ['A', 'B', 'Moment', 'Undo'];
const PRIMITIVES = ['Click', 'Doppio click', 'Triplo click', 'Pressione lunga', 'Simultanea'];
const PRIMITIVE_CHORD = 4;
const MAX_SEQUENCE = 8;
const MAX_MAPPINGS = 16;
const CONFIG_HEADER_SIZE = 11;
const CONFIG_MAPPING_SIZE = 18;

let config = null;
const $ = id => document.getElementById(id);

function diag(event, data) {
  if (data === undefined) console.debug('[Playmaker][AP]', event);
  else console.debug('[Playmaker][AP]', event, data);
}

function diagError(event, error) {
  console.error('[Playmaker][AP]', event, error);
}

function setStatus(text, ok) {
  $('statusText').textContent = text;
  $('statusDot').className = 'dot ' + (ok ? 'ok' : 'bad');
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

async function fetchStatus() {
  const response = await fetch('/api/status', { cache: 'no-store' });
  if (!response.ok) throw new Error('HTTP status ' + response.status);
  const value = await response.json();
  diag('status', value);
  $('ssid').textContent = value.ssid;
  $('ble').textContent = 'BLE: ' + (value.bleAuthenticated ? 'autenticato' : 'non autenticato');
  setStatus('ESP32-C3 connesso', true);
}

function parseConfig(buffer) {
  const dv = new DataView(buffer);

  if (dv.byteLength < CONFIG_HEADER_SIZE ||
      dv.getUint8(0) !== PROTOCOL_VERSION ||
      dv.getUint8(1) !== 4) {
    throw new Error('Config protocol mismatch');
  }

  const count = dv.getUint8(2);
  if (count > MAX_MAPPINGS) throw new Error('Numero gesture non valido');

  const wanted = CONFIG_HEADER_SIZE + count * CONFIG_MAPPING_SIZE + 4 * 6;
  if (dv.byteLength < wanted) throw new Error('Config packet truncated');

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
  const response = await fetch('/api/config', { cache: 'no-store' });
  if (!response.ok) throw new Error('GET /api/config -> ' + response.status);
  config = parseConfig(await response.arrayBuffer());
  diag('config read', config);
  render();
}

async function sendCommand(command) {
  diag('config command', Array.from(command));
  const response = await fetch('/api/config', {
    method: 'POST',
    headers: { 'Content-Type': 'application/octet-stream' },
    body: command,
  });

  if (!response.ok) {
    const message = await response.text();
    throw new Error('POST /api/config -> ' + response.status + ' ' + message);
  }

  await refreshConfig();
  await fetchStatus();
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
          <input type="checkbox"
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
    <span class="step">
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
    <div class="mapping">
      <div class="row">
        <strong>#${index + 1}</strong>
        <select data-action="${index}">
          ${ACTIONS.map((name, action) => option(action, name, action === mapping.action)).join('')}
        </select>
        <span>
          ${mapping.seq.map((value, step) => renderStep(value, index, step)).join('')}
        </span>
        <button data-add-step="${index}">+ step</button>
        <button class="primary" data-save-map="${index}">Salva</button>
        <button class="danger" data-delete-map="${index}">Elimina</button>
      </div>
    </div>`).join('');

  $('sounds').innerHTML = config.sounds.map((sound, index) => `
    <div class="row">
      <strong>${ACTIONS[index]}</strong>
      <label>Hz <input data-sound="${index}" data-field="frequency" type="number" min="100" max="8000" value="${sound.frequency}"></label>
      <label>Duty ‰ <input data-sound="${index}" data-field="duty" type="number" min="0" max="900" value="${sound.duty}"></label>
      <label>Durata ms <input data-sound="${index}" data-field="duration" type="number" min="1" max="2000" value="${sound.duration}"></label>
      <button data-save-sound="${index}">Salva suono</button>
    </div>`).join('');
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
  config.mappings[index].action =
    Number(document.querySelector(`[data-action="${index}"]`).value);

  config.mappings[index].seq =
    config.mappings[index].seq.map((_, stepIndex) =>
      readStepFromDom(index, stepIndex));
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
  await sendCommand(command);
}

async function deleteMapping(index) {
  await sendCommand(new Uint8Array([0x11, index]));
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
  await sendCommand(out);
}

$('saveTiming').onclick = async () => {
  try {
    const out = new Uint8Array(9);
    const dv = new DataView(out.buffer);
    out[0] = 0x13;
    put16(dv, 1, $('seqGap').value);
    put16(dv, 3, $('multiGap').value);
    put16(dv, 5, $('longMs').value);
    put16(dv, 7, $('chordMs').value);
    await sendCommand(out);
  } catch (error) {
    diagError('save timings failed', error);
    setStatus(error.message || String(error), false);
  }
};

$('addMapping').onclick = () => {
  if (!config || config.mappings.length >= MAX_MAPPINGS) return;
  config.mappings.push({ action: 0, seq: [singleToken(0, 0)] });
  render();
};

$('defaults').onclick = async () => {
  try {
    await sendCommand(new Uint8Array([0x14]));
  } catch (error) {
    diagError('reset defaults failed', error);
    setStatus(error.message || String(error), false);
  }
};

document.body.addEventListener('change', event => {
  const element = event.target;
  if (element.dataset.kind !== 'primitive') return;

  const mapIndex = Number(element.dataset.map);
  const stepIndex = Number(element.dataset.step);

  try { syncMappingFromDom(mapIndex); } catch (_) {}

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
      const [index, step] = element.dataset.removeStep.split(':').map(Number);
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
    setStatus(error.message || String(error), false);
  }
});

window.addEventListener('load', async () => {
  try {
    diag('page loaded');
    await fetchStatus();
    await refreshConfig();
  } catch (error) {
    diagError('initialization failed', error);
    setStatus(error.message || String(error), false);
  }
});
