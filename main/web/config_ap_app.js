const PROTOCOL_VERSION = 6;
const ACTIONS = ['Punto A', 'Punto B', 'Moment', 'Undo', 'VAR', 'Fast forward start', 'Fast rewind start', 'Stop', 'Pairing'];
const PAIRING_ACTION = 8;
const SOUND_ACTION_COUNT = 5;
const BUTTONS = ['A', 'B', 'Moment', 'Undo'];
const TYPES = ['Click', 'Doppio click', 'Triplo click', 'Pressione lunga', 'Rilascio dopo long'];
const MAX_SEQUENCE = 8;
const MAX_MAPPINGS = 16;
const CONFIG_HEADER_SIZE = 11;
const CONFIG_MAPPING_SIZE = 18;

let config = null;
const $ = id => document.getElementById(id);

function diag(event, data) {
  data === undefined ? console.debug('[Playmaker][AP]', event)
                     : console.debug('[Playmaker][AP]', event, data);
}
function diagError(event, error) { console.error('[Playmaker][AP]', event, error); }
function u16(dv, offset) { return dv.getUint16(offset, true); }
function put16(dv, offset, value) { dv.setUint16(offset, Number(value), true); }
function token(mask, type) { return ((type & 0x07) << 4) | (mask & 0x0f); }
function tokenMask(value) { return value & 0x0f; }
function tokenType(value) { return (value >> 4) & 0x07; }

function toast(message) {
  const el = $('toast'); el.textContent = message; el.classList.add('show');
  clearTimeout(toast.timer); toast.timer = setTimeout(() => el.classList.remove('show'), 1800);
}
function setStatus(text, ok) {
  $('statusText').textContent = text;
  $('statusDot').className = 'dot ' + (ok ? 'ok' : 'bad');
}
function maskLabel(mask) {
  return BUTTONS.filter((_, i) => mask & (1 << i)).join(' + ') || 'Nessun pulsante';
}
function stepLabel(value) { return maskLabel(tokenMask(value)) + ' · ' + TYPES[tokenType(value)]; }

async function fetchStatus() {
  const response = await fetch('/api/status', { cache: 'no-store' });
  if (!response.ok) throw new Error('HTTP status ' + response.status);
  const value = await response.json();
  $('ssid').textContent = value.ssid;
  $('ble').textContent = 'BLE: ' + (value.bleAuthenticated ? 'autenticato' : 'non autenticato');
  setStatus('ESP32-C3 online', true);
  diag('status', value);
}

function parseConfig(buffer) {
  const dv = new DataView(buffer);
  if (dv.byteLength < CONFIG_HEADER_SIZE ||
      dv.getUint8(0) !== PROTOCOL_VERSION ||
      dv.getUint8(1) !== 4) {
    throw new Error('Versione configurazione non compatibile. Riflashare firmware e ricaricare la pagina.');
  }
  const count = dv.getUint8(2);
  if (count > MAX_MAPPINGS) throw new Error('Numero gesture non valido');
  const wanted = CONFIG_HEADER_SIZE + count * CONFIG_MAPPING_SIZE + SOUND_ACTION_COUNT * 6;
  if (dv.byteLength < wanted) throw new Error('Pacchetto configurazione incompleto');

  const result = {
    mappingCount: count,
    multiGap: u16(dv, 3),
    longMs: u16(dv, 5),
    seqGap: u16(dv, 7),
    simultaneousMs: u16(dv, 9),
    mappings: [], sounds: []
  };
  let offset = CONFIG_HEADER_SIZE;
  for (let i = 0; i < count; i++) {
    const action = dv.getUint8(offset++);
    const length = dv.getUint8(offset++);
    const sequence = [];
    for (let t = 0; t < MAX_SEQUENCE; t++) {
      const value = u16(dv, offset); offset += 2;
      if (t < length) sequence.push(value);
    }
    result.mappings.push({ action, seq: sequence });
  }
  for (let i = 0; i < SOUND_ACTION_COUNT; i++) {
    result.sounds.push({ frequency:u16(dv,offset), duty:u16(dv,offset+2), duration:u16(dv,offset+4) });
    offset += 6;
  }
  return result;
}

async function refreshConfig() {
  const response = await fetch('/api/config', { cache: 'no-store' });
  if (!response.ok) throw new Error('GET /api/config → ' + response.status);
  config = parseConfig(await response.arrayBuffer());
  diag('config read', config);
  render();
}
async function sendCommand(command, message='Salvato') {
  diag('config command', Array.from(command));
  document.body.classList.add('loading');
  try {
    const response = await fetch('/api/config', {
      method:'POST', headers:{'Content-Type':'application/octet-stream'}, body:command
    });
    if (!response.ok) throw new Error('ESP32 ha rifiutato la configurazione: ' + await response.text());
    await refreshConfig(); await fetchStatus(); toast(message);
  } finally { document.body.classList.remove('loading'); }
}

function renderStep(value, mapIndex, stepIndex) {
  const mask = tokenMask(value), type = tokenType(value);
  return `<div class="step">
    <div class="step-head"><strong>STEP ${stepIndex + 1}</strong>
      <button class="ghost" data-remove-step="${mapIndex}:${stepIndex}">Rimuovi</button></div>
    <div class="buttons">
      ${BUTTONS.map((name,i)=>`<button class="button-chip ${mask&(1<<i)?'on':''}"
        data-toggle-button="${mapIndex}:${stepIndex}:${i}">${name}</button>`).join('')}
    </div>
    <div class="step-controls">
      <label>Tipo di pressione<select data-type="${mapIndex}:${stepIndex}">
        ${TYPES.map((name,i)=>`<option value="${i}" ${i===type?'selected':''}>${name}</option>`).join('')}
      </select></label>
      <label>Interpretazione<span style="display:block;margin-top:4px;padding:10px 12px;background:#fff;border:1px solid #ddd;border-radius:12px;color:#444">${stepLabel(value)}</span></label>
    </div>
  </div>`;
}
function render() {
  if (!config) return;
  $('seqGap').value=config.seqGap; $('multiGap').value=config.multiGap;
  $('longMs').value=config.longMs; $('simultaneousMs').value=config.simultaneousMs;

  const pairingCount = config.mappings.filter(m => m.action === PAIRING_ACTION).length;
  $('mappings').innerHTML = config.mappings.length ? config.mappings.map((m,i)=>{
    const summary=m.seq.map(stepLabel).join(' → ');
    const protectsPairing = m.action === PAIRING_ACTION && pairingCount === 1;
    return `<div class="mapping">
      <div class="mapping-head"><div><div class="mapping-title">Gesture ${i+1}</div>
      <div class="mapping-summary">${summary} → ${ACTIONS[m.action]}</div></div>
      ${protectsPairing
        ? '<span class="mapping-summary">Pairing obbligatorio</span>'
        : `<button class="danger" data-delete-map="${i}">Elimina</button>`}
      </div>
      ${m.seq.map((v,s)=>renderStep(v,i,s)).join('')}
      <div class="mapping-actions">
        <select data-action="${i}">${ACTIONS.map((name,a)=>`<option value="${a}" ${a===m.action?'selected':''}>Azione: ${name}</option>`).join('')}</select>
        <button class="ghost" data-add-step="${i}">+ Step</button>
        <button class="primary" data-save-map="${i}">Salva gesture</button>
      </div>
    </div>`;
  }).join('') : '<div class="empty">Nessuna gesture configurata.</div>';

  $('sounds').innerHTML=config.sounds.map((s,i)=>`<div class="sound-row"><strong>${ACTIONS[i]}</strong>
    <label>Frequenza<input data-sound="${i}" data-field="frequency" type="number" min="100" max="8000" value="${s.frequency}"></label>
    <label>Duty ‰<input data-sound="${i}" data-field="duty" type="number" min="0" max="900" value="${s.duty}"></label>
    <label>Durata ms<input data-sound="${i}" data-field="duration" type="number" min="1" max="2000" value="${s.duration}"></label>
    <button data-save-sound="${i}">Salva</button></div>`).join('');
}
function syncMappingFromDom(index) {
  const action=document.querySelector(`[data-action="${index}"]`);
  if(action) config.mappings[index].action=Number(action.value);
  config.mappings[index].seq=config.mappings[index].seq.map((old,step)=>{
    const typeEl=document.querySelector(`[data-type="${index}:${step}"]`);
    return token(tokenMask(old), typeEl?Number(typeEl.value):tokenType(old));
  });
}
async function saveMapping(index) {
  const beforeAction = config.mappings[index].action;
  syncMappingFromDom(index);
  const m=config.mappings[index];
  if(!m.seq.length) throw new Error('La gesture deve avere almeno uno step.');
  if(m.seq.some(v=>tokenMask(v)===0)) throw new Error('Ogni step deve avere almeno un pulsante selezionato.');

  const otherPairing = config.mappings.some((mapping, i) =>
    i !== index && mapping.action === PAIRING_ACTION);
  if (beforeAction === PAIRING_ACTION && m.action !== PAIRING_ACTION && !otherPairing) {
    m.action = beforeAction;
    render();
    throw new Error('Deve esistere sempre almeno una gesture di Pairing. Crea prima la nuova gesture Pairing, poi modifica questa.');
  }
  const out=new Uint8Array(4+2*MAX_SEQUENCE), dv=new DataView(out.buffer);
  out[0]=0x10; out[1]=index; out[2]=m.action; out[3]=m.seq.length;
  m.seq.forEach((v,s)=>put16(dv,4+s*2,v));
  await sendCommand(out,'Gesture salvata');
}
async function saveSound(index) {
  const values={}; document.querySelectorAll(`[data-sound="${index}"]`).forEach(e=>values[e.dataset.field]=Number(e.value));
  const out=new Uint8Array(8),dv=new DataView(out.buffer); out[0]=0x12; out[1]=index;
  put16(dv,2,values.frequency);put16(dv,4,values.duty);put16(dv,6,values.duration);
  await sendCommand(out,'Suono salvato');
}

$('saveTiming').onclick=async()=>{try{
  const out=new Uint8Array(9),dv=new DataView(out.buffer);out[0]=0x13;
  put16(dv,1,$('seqGap').value);put16(dv,3,$('multiGap').value);
  put16(dv,5,$('longMs').value);put16(dv,7,$('simultaneousMs').value);
  await sendCommand(out,'Timing salvati');
}catch(e){diagError('timing',e);setStatus(e.message,false)}};

$('addMapping').onclick=()=>{if(!config||config.mappings.length>=MAX_MAPPINGS)return;
  config.mappings.push({action:0,seq:[token(1,0)]});render();setTimeout(()=>window.scrollTo({top:document.body.scrollHeight,behavior:'smooth'}),20)};

$('defaults').onclick=async()=>{if(!confirm('Ripristinare mapping e timing predefiniti?'))return;
  try{await sendCommand(new Uint8Array([0x14]),'Default ripristinati')}catch(e){diagError('defaults',e);setStatus(e.message,false)}};

document.body.addEventListener('change',e=>{
  if(!e.target.dataset.type)return;
  const [m,s]=e.target.dataset.type.split(':').map(Number);
  config.mappings[m].seq[s]=token(tokenMask(config.mappings[m].seq[s]),Number(e.target.value));render();
});
document.body.addEventListener('click',async e=>{const el=e.target;try{
  if(el.dataset.toggleButton){const[m,s,b]=el.dataset.toggleButton.split(':').map(Number);const old=config.mappings[m].seq[s];config.mappings[m].seq[s]=token(tokenMask(old)^(1<<b),tokenType(old));render()}
  if(el.dataset.addStep!==undefined){const i=Number(el.dataset.addStep);syncMappingFromDom(i);if(config.mappings[i].seq.length<MAX_SEQUENCE)config.mappings[i].seq.push(token(1,0));render()}
  if(el.dataset.removeStep!==undefined){const[i,s]=el.dataset.removeStep.split(':').map(Number);syncMappingFromDom(i);if(config.mappings[i].seq.length>1)config.mappings[i].seq.splice(s,1);else throw new Error('Una gesture deve avere almeno uno step.');render()}
  if(el.dataset.saveMap!==undefined)await saveMapping(Number(el.dataset.saveMap));
  if(el.dataset.deleteMap!==undefined)await sendCommand(new Uint8Array([0x11,Number(el.dataset.deleteMap)]),'Gesture eliminata');
  if(el.dataset.saveSound!==undefined)await saveSound(Number(el.dataset.saveSound));
}catch(err){diagError('ui',err);setStatus(err.message||String(err),false)}});

window.addEventListener('load',async()=>{try{await fetchStatus();await refreshConfig()}catch(e){
  diagError('init',e);setStatus(e.message||String(e),false);
  $('mappings').innerHTML='<div class="empty">Impossibile caricare la configurazione. Controlla la console del browser.</div>';
}});
