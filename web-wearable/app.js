const PROTOCOL_VERSION = 5;
const SERVICE = 'c4d10001-6f65-4b6e-ae30-706c61796d6b';
const UUID = {
  action:'c4d10002-6f65-4b6e-ae30-706c61796d6b',
  ack:'c4d10003-6f65-4b6e-ae30-706c61796d6b',
  control:'c4d10004-6f65-4b6e-ae30-706c61796d6b',
  status:'c4d10005-6f65-4b6e-ae30-706c61796d6b',
};
const ACTION = { POINT_A:0, POINT_B:1, MOMENT:2, UNDO:3, VAR:4 };
const ACK = { OK:0, REJECTED:1, TEMP:2 };
const FLAG = { GAME:1, SET:2, MATCH:4 };
const RETRY_MS=[500,500,1000,1000,2000,3000,5000,8000];

let device=null, chars={}, reconnectAttempt=0, reconnectTimer=null, suppressReconnect=false;
const ackCache=new Map();
let history=[];
let state=freshState();

const $=id=>document.getElementById(id);
function diag(e,d){d===undefined?console.debug('[Playmaker][BLE]',e):console.debug('[Playmaker][BLE]',e,d)}
function err(e,d){console.error('[Playmaker][BLE]',e,d)}
function eventText(t){$('lastEvent').textContent=t;diag(t)}
function setBle(t,ok=false){$('bleStatus').textContent=t;$('bleDot').className='dot '+(ok?'ok':'bad')}

function freshState(){return {revision:0,setsA:0,setsB:0,gamesA:0,gamesB:0,pointsA:0,pointsB:0,tiebreak:false,tbA:0,tbB:0,matchOver:false}}
function clone(s){return JSON.parse(JSON.stringify(s))}
function pointText(p){return ['0','15','30','40','AD'][p] ?? String(p)}
function render(){
  $('setsA').textContent=state.setsA;$('setsB').textContent=state.setsB;
  $('gamesA').textContent=state.gamesA;$('gamesB').textContent=state.gamesB;
  $('pointsA').textContent=state.tiebreak?state.tbA:pointText(state.pointsA);
  $('pointsB').textContent=state.tiebreak?state.tbB:pointText(state.pointsB);
  $('tbLabel').style.display=state.tiebreak?'block':'none';
  $('tbLabel').textContent=state.tiebreak?'Tie-break in corso':'';
}
function gameWon(team){
  if(team==='A')state.gamesA++;else state.gamesB++;
  state.pointsA=state.pointsB=0;
  let flags=FLAG.GAME;
  const a=state.gamesA,b=state.gamesB;
  const setWon=(a>=6||b>=6)&&Math.abs(a-b)>=2;
  if(setWon) flags|=finishSet(a>b?'A':'B');
  else if(a===6&&b===6){state.tiebreak=true;state.tbA=state.tbB=0}
  return flags;
}
function finishSet(team){
  if(team==='A')state.setsA++;else state.setsB++;
  state.gamesA=state.gamesB=0;state.pointsA=state.pointsB=0;state.tiebreak=false;state.tbA=state.tbB=0;
  let f=FLAG.SET;
  if(state.setsA>=2||state.setsB>=2){state.matchOver=true;f|=FLAG.MATCH}
  return f;
}
function addNormalPoint(team){
  let a=state.pointsA,b=state.pointsB;
  if(team==='A'){
    if(a<=2){state.pointsA++;return 0}
    if(a===3&&b<=2)return gameWon('A');
    if(a===3&&b===3){state.pointsA=4;return 0}
    if(a===4)return gameWon('A');
    if(b===4){state.pointsB=3;return 0}
  }else{
    if(b<=2){state.pointsB++;return 0}
    if(b===3&&a<=2)return gameWon('B');
    if(b===3&&a===3){state.pointsB=4;return 0}
    if(b===4)return gameWon('B');
    if(a===4){state.pointsA=3;return 0}
  }
  return 0;
}
function addTiebreakPoint(team){
  if(team==='A')state.tbA++;else state.tbB++;
  const a=state.tbA,b=state.tbB;
  if((a>=7||b>=7)&&Math.abs(a-b)>=2){
    if(a>b)state.gamesA=7;else state.gamesB=7;
    return FLAG.GAME|finishSet(a>b?'A':'B');
  }
  return 0;
}
function applyAction(action){
  if(action===ACTION.MOMENT||action===ACTION.VAR)return {status:ACK.OK,flags:0};
  if(action===ACTION.UNDO){
    if(!history.length)return {status:ACK.REJECTED,flags:0};
    const previous=history.pop();const revision=state.revision+1;state=previous;state.revision=revision;render();
    return {status:ACK.OK,flags:0};
  }
  if(state.matchOver)return {status:ACK.REJECTED,flags:0};
  history.push(clone(state));
  let flags=state.tiebreak?addTiebreakPoint(action===ACTION.POINT_A?'A':'B'):addNormalPoint(action===ACTION.POINT_A?'A':'B');
  state.revision++;render();
  return {status:ACK.OK,flags};
}
function pointByte(p){return [0,15,30,40,50][p]??p}
function makeAck(session,sequence,status,flags){
  const out=new Uint8Array(20),dv=new DataView(out.buffer);
  out[0]=PROTOCOL_VERSION;out[1]=2;out[2]=status;out[3]=flags;
  dv.setUint32(4,session,true);dv.setUint32(8,sequence,true);dv.setUint16(12,state.revision&0xffff,true);
  out[14]=state.tiebreak?state.tbA:pointByte(state.pointsA);out[15]=state.tiebreak?state.tbB:pointByte(state.pointsB);
  out[16]=state.gamesA;out[17]=state.gamesB;out[18]=state.setsA;out[19]=state.setsB;
  return out;
}

function randomToken(){const v=new Uint8Array(16);crypto.getRandomValues(v);return v}
function tokenKey(){return device?`playmaker-token:${device.id}`:''}
function loadToken(){const raw=localStorage.getItem(tokenKey());if(!raw)return null;const v=raw.split(',').map(Number);return v.length===16?new Uint8Array(v):null}
function saveToken(t){localStorage.setItem(tokenKey(),Array.from(t).join(','))}
function controlPacket(op,t){const out=new Uint8Array(17);out[0]=op;out.set(t,1);return out}
async function writeChar(ch,bytes){diag('GATT write',{uuid:ch.uuid,bytes:Array.from(bytes)});return ch.writeValueWithResponse?ch.writeValueWithResponse(bytes):ch.writeValue(bytes)}
function decodeStatus(dv){
  if(dv.byteLength<10||dv.getUint8(0)!==PROTOCOL_VERSION||dv.getUint8(1)!==3)throw new Error('Status protocol mismatch');
  return {commissioned:!!dv.getUint8(2),authenticated:!!dv.getUint8(3),pairingOpen:!!dv.getUint8(4),batteryMv:dv.getUint16(5,true)}
}
function paintStatus(s){$('commissioned').textContent=s.commissioned?'Sì':'No';$('authenticated').textContent=s.authenticated?'Sì':'No';$('pairingOpen').textContent=s.pairingOpen?'Aperto':'Chiuso'}
async function readStatus(){const s=decodeStatus(await chars.status.readValue());paintStatus(s);diag('status',s);return s}
function onStatusNotification(ev){try{const s=decodeStatus(ev.target.value);paintStatus(s);if(!s.commissioned&&s.pairingOpen){suppressReconnect=true;clearTimeout(reconnectTimer);if(device)localStorage.removeItem(tokenKey());eventText('Pairing fisico aperto: vecchia associazione invalidata.')}}catch(e){err('status notify',e)}}
async function authenticateOrClaim(allowClaim){
  const s=await readStatus();let token=loadToken();
  if(s.commissioned){
    if(!token)throw new Error('Wearable già associato: esegui prima la gesture fisica di Pairing.');
    await writeChar(chars.control,controlPacket(0x02,token));
  }else{
    if(!s.pairingOpen)throw new Error('Pairing chiuso: esegui la gesture fisica di Pairing.');
    if(!allowClaim)throw new Error('Pairing aperto: usa Connetti / Pair.');
    if(!token){token=randomToken();saveToken(token)}
    await writeChar(chars.control,controlPacket(0x01,token));
  }
  const after=await readStatus();if(!after.authenticated)throw new Error('Autenticazione fallita.');
}
async function connectDevice(target,{allowClaim=false}={}){
  clearTimeout(reconnectTimer);device=target;device.removeEventListener('gattserverdisconnected',onDisconnected);device.addEventListener('gattserverdisconnected',onDisconnected);
  setBle('Connessione…');
  const server=await device.gatt.connect(),service=await server.getPrimaryService(SERVICE);
  chars={action:await service.getCharacteristic(UUID.action),ack:await service.getCharacteristic(UUID.ack),control:await service.getCharacteristic(UUID.control),status:await service.getCharacteristic(UUID.status)};
  await chars.status.startNotifications();chars.status.addEventListener('characteristicvaluechanged',onStatusNotification);
  await chars.action.startNotifications();chars.action.addEventListener('characteristicvaluechanged',onAction);
  await authenticateOrClaim(allowClaim);reconnectAttempt=0;suppressReconnect=false;setBle(device.name,true);eventText('BLE autenticato: '+device.name);
}
async function requestConnect(){const target=await navigator.bluetooth.requestDevice({filters:[{services:[SERVICE]}]});await connectDevice(target,{allowClaim:true})}
async function findGrantedDevice(){if(!navigator.bluetooth.getDevices)return null;const ds=await navigator.bluetooth.getDevices();return ds.find(d=>d.name?.startsWith('PLAYMAKER_'))??null}
async function autoReconnect(){if(suppressReconnect)return false;try{const d=device??await findGrantedDevice();if(!d)return false;await connectDevice(d,{allowClaim:false});return true}catch(e){err('reconnect',e);setBle(e.message);return false}}
function onDisconnected(){setBle('Disconnesso');if(suppressReconnect)return;const delay=RETRY_MS[Math.min(reconnectAttempt++,RETRY_MS.length-1)];reconnectTimer=setTimeout(async()=>{if(!(await autoReconnect()))onDisconnected()},delay)}
async function onAction(ev){
  try{
    const dv=ev.target.value;if(dv.byteLength!==12||dv.getUint8(0)!==PROTOCOL_VERSION||dv.getUint8(1)!==1)return;
    const session=dv.getUint32(2,true),sequence=dv.getUint32(6,true),action=dv.getUint8(10),key=`${session}:${sequence}`;
    let ack=ackCache.get(key);
    if(!ack){
      const result=applyAction(action);ack=makeAck(session,sequence,result.status,result.flags);ackCache.set(key,ack);
      if(ackCache.size>64)ackCache.delete(ackCache.keys().next().value);
      eventText(`ACTION ${['POINT_A','POINT_B','MOMENT','UNDO','VAR'][action]??action} · seq ${sequence} · flags 0x${result.flags.toString(16)}`);
    }else eventText(`Retry seq ${sequence}: restituito ACK cached`);
    await writeChar(chars.ack,ack);
  }catch(e){err('ACTION/ACK',e);eventText('Errore ACTION/ACK: '+e.message)}
}

$('connect').onclick=async()=>{try{await requestConnect()}catch(e){err('connect',e);setBle(e.message);eventText(e.message)}};
$('auto').onclick=async()=>{suppressReconnect=false;if(!(await autoReconnect()))eventText('Nessun device autorizzato o riconnessione fallita.')};
$('manualA').onclick=()=>{const r=applyAction(ACTION.POINT_A);eventText('Punto A manuale · flags 0x'+r.flags.toString(16))};
$('manualB').onclick=()=>{const r=applyAction(ACTION.POINT_B);eventText('Punto B manuale · flags 0x'+r.flags.toString(16))};
$('manualUndo').onclick=()=>{const r=applyAction(ACTION.UNDO);eventText(r.status===ACK.OK?'Undo manuale':'Niente da annullare')};
$('resetMatch').onclick=()=>{state=freshState();history=[];ackCache.clear();render();eventText('Nuova partita')};
$('clearBrowserToken').onclick=()=>{if(device)localStorage.removeItem(tokenKey());eventText('Token locale eliminato. Per riassociare serve la gesture fisica Pairing.')};

window.addEventListener('load',()=>{
  render();
  if(location.protocol==='file:')eventText('Apri questa pagina tramite http://localhost, non come file://');
  if(!navigator.bluetooth){setBle('Web Bluetooth non disponibile');eventText('Usa Chrome/Edge desktop via localhost o HTTPS.')}
});
