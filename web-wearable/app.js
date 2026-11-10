const PROTOCOL_VERSION = 6;
const ACTION = { POINT_A:0, POINT_B:1, MOMENT:2, UNDO:3, VAR:4, FAST_FORWARD_START:5, FAST_REWIND_START:6, STOP:7 };
const ACK = { OK:0, REJECTED:1, TEMP:2 };
const FLAG = { GAME:1, SET:2, MATCH:4 };

let ackCache=new Map();
let history=[];
let state=freshState();

const $=id=>document.getElementById(id);
function diag(e,d){d===undefined?console.debug('[Playmaker][Wi-Fi]',e):console.debug('[Playmaker][Wi-Fi]',e,d)}
function err(e,d){console.error('[Playmaker][Wi-Fi]',e,d)}
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
  $('tbLabel').hidden=!state.tiebreak;
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
  if(action===ACTION.MOMENT||action===ACTION.VAR||action===ACTION.FAST_FORWARD_START||action===ACTION.FAST_REWIND_START||action===ACTION.STOP)return {status:ACK.OK,flags:0};
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


const clientId=Array.from(crypto.getRandomValues(new Uint8Array(16)),n=>n.toString(16).padStart(2,'0')).join('');
let running=false,pollTimer,storageKey='',ready=false,connecting=false;

async function api(path,options={}) {
  const response=await fetch(path,{...options,cache:'no-store',signal:AbortSignal.timeout(path==='/api/ack'?800:6000),
    headers:{'X-Playmaker-Client':clientId,...options.headers}});
  if(!response.ok){const e=new Error(await response.text());e.status=response.status;throw e}
  return response;
}
function persist(){localStorage.setItem(storageKey,JSON.stringify({state,history,acks:Array.from(ackCache,([k,v])=>[k,Array.from(v)])}))}
function restore(){
  const raw=localStorage.getItem(storageKey);if(!raw)return;
  const saved=JSON.parse(raw);
  if(!saved.state||!Array.isArray(saved.history)||!Array.isArray(saved.acks))throw new Error('Partita salvata non valida');
  state=saved.state;history=saved.history;ackCache=new Map(saved.acks.map(([k,v])=>[k,new Uint8Array(v)]));render();
}
async function networkStatus(){
  const response=await fetch('/api/network',{cache:'no-store',signal:AbortSignal.timeout(6000)});
  if(!response.ok)throw new Error('Apri il tabellone all’indirizzo del wearable, non su localhost.');
  const info=await response.json();
  $('networkName').textContent=info.connected?info.ssid:'Collegamento diretto al wearable';
  $('networkIp').textContent=info.ip||'192.168.4.1';$('deviceName').textContent=info.name;
  $('setupHint').hidden=info.connected;
  if(!ready){storageKey='playmaker-wifi-match:'+info.name;restore();ready=true}
}
async function connect(){
  if(connecting)return;
  connecting=true;running=false;clearTimeout(pollTimer);
  try{
    await networkStatus();persist();await api('/api/session',{method:'POST'});
    running=true;setBle('Wearable connesso',true);$('connect').textContent='Connesso';
    pollTimer=setTimeout(poll,0);
  }finally{connecting=false}
}
async function poll(){
  if(!running)return;
  try{
    const response=await api('/api/action');
    if(response.status!==204){
      const dv=new DataView(await response.arrayBuffer());
      if(dv.byteLength!==12||dv.getUint8(0)!==PROTOCOL_VERSION||dv.getUint8(1)!==1)throw new Error('Messaggio non compatibile');
      const session=dv.getUint32(2,true),sequence=dv.getUint32(6,true),action=dv.getUint8(10),key=session+':'+sequence;
      if(action>ACTION.STOP)throw new Error('Azione sconosciuta');
      let ack=ackCache.get(key);
      if(!ack){
        const previous=clone(state),previousHistory=history.slice();
        const result=applyAction(action);ack=makeAck(session,sequence,result.status,result.flags);ackCache.set(key,ack);
        if(ackCache.size>64)ackCache.delete(ackCache.keys().next().value);
        try{persist()}catch(e){state=previous;history=previousHistory;ackCache.delete(key);render();throw e}
        eventText(['Punto squadra A','Punto squadra B','Momento segnato','Ultimo punto annullato','VAR','Avanti veloce','Indietro veloce','Stop'][action]);
      }
      await api('/api/ack',{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:ack});
    }
    setBle('Wearable connesso',true);pollTimer=setTimeout(poll,150);
  }catch(e){
    err('connessione',e);setBle('Connessione interrotta. Riprovo…');
    if(e.status===410){eventText(e.message);pollTimer=setTimeout(poll,150);return}
    if(e.status===409){running=false;setBle(e.message);$('connect').textContent='Connetti';pollTimer=setTimeout(reconnect,2000);return}
    pollTimer=setTimeout(poll,150);
  }
}
async function reconnect(){
  try{await connect()}catch(e){setBle(e.message);$('connect').textContent='Riprova';pollTimer=setTimeout(reconnect,2000)}
}
$('connect').onclick=reconnect;
function manual(action){
  if(!ready||!running)return;
  const before=clone(state),beforeHistory=history.slice(),result=applyAction(action);
  try{persist();eventText(result.status===ACK.OK?'Punteggio aggiornato':'Niente da annullare')}
  catch(e){state=before;history=beforeHistory;render();eventText('Salvataggio non disponibile: punto non registrato')}
}
$('manualA').onclick=()=>manual(ACTION.POINT_A);
$('manualB').onclick=()=>manual(ACTION.POINT_B);
$('manualUndo').onclick=()=>manual(ACTION.UNDO);
$('resetMatch').onclick=()=>{
  if(!ready||!running||!confirm('Iniziare una nuova partita?'))return;
  state=freshState();history=[];persist();render();eventText('Nuova partita');
};
render();reconnect();
setInterval(()=>networkStatus().catch(()=>{}),10000);
