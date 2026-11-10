const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const { webcrypto } = require('node:crypto');

async function main() {
  const elements = new Map();
  const storage = new Map();
  let action = null, ackFailures = 0, acks = 0;
  const context = vm.createContext({
    console, crypto: webcrypto, Uint8Array, DataView, AbortSignal,
    setTimeout: () => 1, clearTimeout() {}, setInterval() {}, confirm: () => true,
    localStorage: {getItem: key => storage.get(key) || null, setItem: (key, value) => storage.set(key, value)},
    document: {getElementById: id => {
      if (!elements.has(id)) elements.set(id, {textContent:'',className:'',hidden:false,style:{}});
      return elements.get(id);
    }},
    fetch: async (url, options = {}) => {
      if (url === '/api/network') return Response.json({name:'PLAYMAKER_TEST',connected:true,ssid:'Campo',ip:'192.168.1.50'});
      if (url === '/api/session') return new Response(null,{status:204});
      if (url === '/api/action') return new Response(action,{status:action ? 200 : 204});
      if (url === '/api/ack') {
        assert.equal(options.body.length,20);
        if (ackFailures-- > 0) throw new Error('Risposta ACK persa');
        acks++;return new Response(null,{status:204});
      }
      throw new Error('Unexpected endpoint '+url);
    },
  });
  vm.runInContext(fs.readFileSync('web-wearable/app.js','utf8'),context);
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(vm.runInContext('running',context),true);
  function packet(sequence, code) {
    const bytes=new Uint8Array(12),v=new DataView(bytes.buffer);
    bytes[0]=6;bytes[1]=1;v.setUint32(2,123,true);v.setUint32(6,sequence,true);bytes[10]=code;
    return bytes;
  }
  action=packet(1,0);ackFailures=1;
  await context.poll();
  assert.equal(vm.runInContext('state.pointsA',context),1);
  assert.ok([...storage.values()][0].includes('123:1'),'ACK persisted before sending');
  await context.poll();
  assert.equal(vm.runInContext('state.pointsA',context),1,'retry cannot add a second point');
  assert.equal(acks,1);
  action=packet(2,1);await context.poll();
  assert.equal(vm.runInContext('state.pointsB',context),1);
  action=packet(3,3);await context.poll();
  assert.equal(vm.runInContext('state.pointsB',context),0,'undo restores previous state');
  action=packet(4,0);
  vm.runInContext('localStorage.setItem=()=>{throw new Error("storage full")}',context);
  await context.poll();
  assert.equal(vm.runInContext('state.pointsA',context),1,'failed persistence rolls score back');
  assert.equal(acks,3,'no ACK when local persistence failed');
  console.log('Wi-Fi scoreboard: lost ACK, retry deduplication, undo and storage failure OK');
}
main().catch(error=>{console.error(error);process.exitCode=1});
