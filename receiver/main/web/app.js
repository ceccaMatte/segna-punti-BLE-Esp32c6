const $=id=>document.getElementById(id);
let dirtyMode=false,actualMode='',scanning=false;
function message(text){$('message').hidden=false;$('message').textContent=text}
async function json(path,options={}){
  const r=await fetch(path,{cache:'no-store',signal:AbortSignal.timeout(15000),...options});
  if(!r.ok)throw new Error(await r.text());return r;
}
function showMode(mode){$('directHint').hidden=mode!=='direct';$('lanSettings').hidden=mode!=='lan'}
async function refresh(){
  const [r,n]=await Promise.all([(await json('/api/receiver')).json(),(await json('/api/network')).json()]);
  actualMode=r.mode;if(!dirtyMode)$('mode').value=r.mode;showMode($('mode').value);
  $('status').textContent=n.name+' · '+(r.mode==='direct'?'Rete diretta':n.connected?'LAN connessa':'LAN non connessa');
  $('network').textContent=n.message+(n.ip?' · http://'+n.ip+'/':'');
  $('usb').textContent=r.pc_ready?'Programma Ubuntu pronto. Inoltro dei comandi attivo.':'In attesa di hello / ready dal programma Ubuntu.';
  $('selected').textContent=r.wearable_id?'Selezionato: '+r.wearable_id+(r.wearable_connected?' · raggiungibile':' · in attesa della connessione'):'Nessun wearable selezionato.';
  const chosen=$('wearable').value;$('wearable').replaceChildren(new Option('Scegli il wearable',''));
  for(const w of r.wearables)$('wearable').add(new Option(w.id+' · '+w.ip,w.id));
  $('wearable').value=r.wearables.some(w=>w.id===chosen)?chosen:r.wearables.some(w=>w.id===r.wearable_id)?r.wearable_id:'';
  $('info').replaceChildren();
  for(const [label,value] of [['Ricevitore',r.receiver_id],['IP wearable',r.wearable_ip||'—'],['Rete',r.ip||'192.168.8.1']]){
    const item=document.createElement('div');item.className='mini';const name=document.createElement('strong'),text=document.createElement('span');name.textContent=label;text.textContent=value;item.append(name,text);$('info').append(item);
  }
}
$('mode').onchange=()=>{dirtyMode=true;showMode($('mode').value)};
$('saveMode').onclick=async()=>{try{
  if($('mode').value===actualMode){dirtyMode=false;return}
  const r=await json('/api/receiver',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({mode:$('mode').value})});
  dirtyMode=false;message(await r.text());
}catch(e){message(e.message)}};
$('select').onclick=async()=>{try{
  if(!$('wearable').value)throw new Error('Seleziona un wearable rilevato.');
  const r=await json('/api/receiver',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({wearable_id:$('wearable').value})});message(await r.text());await refresh();
}catch(e){message(e.message)}};
$('scan').onclick=async()=>{
  if(scanning)return;scanning=true;$('scan').disabled=true;
  try{
    if(actualMode!=='lan')throw new Error('Salva prima la modalità LAN e attendi il riavvio.');
    const networks=await (await json('/api/networks')).json(),selected=$('ssid').value;
    $('ssid').replaceChildren(new Option('Scegli la rete',''));const seen=new Set();
    for(const n of networks)if(!seen.has(n.ssid)){seen.add(n.ssid);$('ssid').add(new Option(n.ssid+' · '+n.rssi+' dBm',n.ssid))}
    if(seen.has(selected))$('ssid').value=selected;
  }catch(e){message(e.message)}finally{scanning=false;$('scan').disabled=false}
};
$('connect').onclick=async()=>{try{
  if(actualMode!=='lan')throw new Error('Salva prima la modalità LAN e attendi il riavvio.');
  const encoder=new TextEncoder(),ssid=encoder.encode($('ssid').value),password=encoder.encode($('password').value);
  if(!ssid.length||ssid.length>32||password.length>64||(password.length&&password.length<8))throw new Error('Seleziona la rete e inserisci una password valida.');
  const body=new Uint8Array(98);body.set(ssid);body.set(password,33);
  const r=await json('/api/network',{method:'POST',headers:{'Content-Type':'application/octet-stream'},body});message(await r.text());$('password').value='';
}catch(e){message(e.message)}};
refresh().catch(e=>message(e.message));
setInterval(()=>refresh().catch(()=>$('status').textContent='Ricevitore non raggiungibile. Ricollegati al suo Wi-Fi dopo il riavvio.'),3000);
