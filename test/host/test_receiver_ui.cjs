const {chromium}=require('../../build/qa/node_modules/playwright');
const fs=require('node:fs');
const assert=require('node:assert/strict');
async function main(){
  const browser=await chromium.launch({executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true});
  try{for(const width of [390,1440]){
    let mode='direct',savedNetwork=false,selected=false;
    const page=await browser.newPage({viewport:{width,height:1000}}),errors=[];
    page.on('pageerror',e=>errors.push(e.message));
    await page.route('http://receiver.test/**',async route=>{
      const request=route.request(),path=new URL(request.url()).pathname;
      const files={'/':'receiver/main/web/index.html','/app.js':'receiver/main/web/app.js','/brand.css':'web-wearable/brand.css'};
      if(files[path])return route.fulfill({body:fs.readFileSync(files[path]),contentType:path.endsWith('.js')?'application/javascript':path.endsWith('.css')?'text/css':'text/html'});
      if(path==='/api/receiver'){
        if(request.method()==='POST'){
          const body=request.postDataJSON();if(body.mode)mode=body.mode;else if(body.wearable_id)selected=true;
          return route.fulfill({body:'Salvato'});
        }
        return route.fulfill({json:{v:1,type:'status',mode,receiver_id:'ac276e45e004',pc_ready:false,ip:'192.168.8.1',wearable_id:selected?'ac276e45e006':null,wearable_ip:null,wearable_connected:false,wearables:[{id:'ac276e45e006',ip:'192.168.8.2'}]}});
      }
      if(path==='/api/network'){
        if(request.method()==='POST'){assert.equal(request.postDataBuffer().length,98);savedNetwork=true;return route.fulfill({body:'Rete salvata'})}
        return route.fulfill({json:{name:'PLAYMAKER_RX_E004',message:'Pronto',connected:mode==='lan',ip:mode==='lan'?'192.168.1.50':''}});
      }
      if(path==='/api/networks')return route.fulfill({json:[{ssid:'Campo',rssi:-42}]});
      return route.fulfill({status:404});
    });
    await page.goto('http://receiver.test/');
    await page.getByText('PLAYMAKER_RX_E004 · Rete diretta',{exact:true}).waitFor();
    assert.equal(await page.locator('#lanSettings').isVisible(),false);
    await page.locator('#mode').selectOption('lan');await page.getByRole('button',{name:'Salva modalità'}).click();
    await page.reload();await page.getByText('PLAYMAKER_RX_E004 · LAN connessa',{exact:true}).waitFor();
    await page.getByRole('button',{name:'Cerca reti'}).click();
    await page.locator('#ssid option[value="Campo"]').waitFor({state:'attached'});await page.locator('#ssid').selectOption('Campo');
    await page.locator('#password').fill('test-only-password');await page.getByRole('button',{name:'Collega e memorizza'}).click();
    await page.getByText('Rete salvata',{exact:true}).waitFor();assert.equal(savedNetwork,true);
    await page.locator('#wearable').selectOption('ac276e45e006');await page.getByRole('button',{name:'Usa questo wearable'}).click();
    await page.getByText('Selezionato: ac276e45e006 · in attesa della connessione',{exact:true}).waitFor();assert.equal(selected,true);
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
    await page.screenshot({path:'build/qa/receiver-'+width+'.png',fullPage:true});
    assert.deepEqual(errors,[]);await page.close();
  }console.log('Receiver UI: direct/LAN, scan, credentials and wearable selection OK on desktop/mobile');}finally{await browser.close()}
}
main().catch(e=>{console.error(e);process.exitCode=1});
