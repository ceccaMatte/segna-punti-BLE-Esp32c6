const {chromium}=require('../../build/qa/node_modules/playwright');
const fs=require('node:fs');
const assert=require('node:assert/strict');

async function main(){
  const browser=await chromium.launch({executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true});
  try{
    for(const mobile of [false,true]){
      const page=await browser.newPage({viewport:mobile?{width:390,height:844}:{width:1440,height:1100}});
      const errors=[];page.on('pageerror',e=>errors.push(e.message));
      await page.route('http://playmaker.test/**',async route=>{
        const path=new URL(route.request().url()).pathname;
        const files={'/':'web-wearable/index.html','/app.js':'web-wearable/app.js','/brand.css':'web-wearable/brand.css','/settings':'main/web/config_ap_page.html','/settings.js':'main/web/config_ap_app.js'};
        if(files[path])return route.fulfill({body:fs.readFileSync(files[path]),contentType:path.endsWith('.js')?'application/javascript':path.endsWith('.css')?'text/css':'text/html'});
        if(path==='/api/network'){
          if(route.request().method()==='POST'){
            assert.equal(route.request().postDataBuffer().length,98);
            return route.fulfill({status:202,body:'Connessione avviata'});
          }
          return route.fulfill({json:{name:'PLAYMAKER_E006',connected:true,ssid:'Campo Playmaker',ip:'192.168.1.50',message:'Connesso alla LAN'}});
        }
        if(path==='/api/networks')return route.fulfill({json:[{ssid:'Campo Playmaker',rssi:-42,secured:true},{ssid:'<script>not markup</script>',rssi:-60,secured:false}]});
        if(path==='/api/session'||path==='/api/action')return route.fulfill({status:204});
        if(path==='/api/status')return route.fulfill({json:{ssid:'Playmaker Wi-Fi',controllerConnected:true}});
        if(path==='/api/config'){
          const bytes=new Uint8Array(11+6*18+30),view=new DataView(bytes.buffer);
          bytes[0]=6;bytes[1]=4;bytes[2]=6;
          [280,450,300,60].forEach((value,i)=>view.setUint16(3+i*2,value,true));
          const tokens=[1,2,4,8,0x14,0x38],actions=[0,1,2,3,4,8];
          tokens.forEach((token,i)=>{const start=11+i*18;bytes[start]=actions[i];bytes[start+1]=1;view.setUint16(start+2,token,true)});
          return route.fulfill({body:Buffer.from(bytes)});
        }
        return route.fulfill({status:404});
      });
      await page.goto('http://playmaker.test/');
      await page.getByText('Wearable connesso',{exact:true}).waitFor();
      await page.getByRole('button',{name:'+ Punto A',exact:true}).click();
      assert.equal(await page.locator('#pointsA').textContent(),'15');
      assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
      await page.screenshot({path:'build/qa/score-'+(mobile?'mobile':'desktop')+'.png',fullPage:true});
      await page.goto('http://playmaker.test/settings');
      await page.getByText('Centrale doppio click → VAR',{exact:true}).waitFor();
      assert.deepEqual(await page.locator('[data-type="0:0"] option').evaluateAll(nodes=>nodes.map(n=>n.value)),['0','3']);
      await page.locator('#wifiSsid option[value="Campo Playmaker"]').waitFor({state:'attached'});
      await page.locator('#wifiSsid').selectOption('Campo Playmaker');
      await page.locator('#wifiPassword').fill('test-only-password');
      await page.getByRole('button',{name:'Collega e memorizza'}).click();
      await page.getByText('Connessione avviata',{exact:true}).waitFor();
      assert.equal(await page.locator('#wifiPassword').inputValue(),'');
      assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
      await page.screenshot({path:'build/qa/setup-'+(mobile?'mobile':'desktop')+'.png',fullPage:true});
      assert.deepEqual(errors,[]);await page.close();
    }
    console.log('Desktop/mobile: score controls, network scan, credentials payload and layout OK');
  }finally{await browser.close()}
}
main().catch(e=>{console.error(e);process.exitCode=1});
