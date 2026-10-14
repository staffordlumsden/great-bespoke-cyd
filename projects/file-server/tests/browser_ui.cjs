const fs = require('fs');
const path = require('path');
const http = require('http');
const assert = require('assert');
const {chromium} = require('playwright');
const project = path.resolve(__dirname, '..');
const assets = path.join(project, 'firmware/web');
const output = path.join(project, 'artifacts/browser');
fs.mkdirSync(output, {recursive:true});
let authenticated = false;
let progress = {busy:false,id:'',phase:'idle',done:0,total:0,elapsed_ms:0,name:'',message:''};
const entries = [];
const server = http.createServer(async (req,res) => {
  const url = new URL(req.url, 'http://localhost');
  const json = (value,status=200) => {res.writeHead(status, {'Content-Type':'application/json'});res.end(JSON.stringify(value));};
  if(url.pathname === '/' || url.pathname.endsWith('.woff2')) {
    const file = url.pathname === '/' ? 'app.html' : path.basename(url.pathname);
    res.writeHead(200, {'Content-Type':file.endsWith('.woff2')?'font/woff2':'text/html'});
    res.end(fs.readFileSync(path.join(assets,file)));return;
  }
  if(url.pathname === '/api/login') {authenticated=true;json({ok:true});return;}
  if(!authenticated){json({error:'Enter the access code'},401);return;}
  if(url.pathname === '/api/status'){json({...progress,connected:true,setup:false,ip:'192.0.2.42',ssid:'Example LAN',card_free:29*1073741824,card_total:32*1073741824});return;}
  if(url.pathname === '/api/list'){json({entries,more:false});return;}
  if(url.pathname === '/api/networks'){json({scanning:false,networks:[{ssid:'Example LAN',rssi:-48,auth:3}]});return;}
  if(url.pathname === '/api/scan'||url.pathname === '/api/join'){json({ok:true});return;}
  if(url.pathname === '/api/cancel'){progress={...progress,busy:false,phase:'cancelled'};json({ok:true});return;}
  if(url.pathname === '/api/file' && req.method==='POST'){
    const name=url.searchParams.get('path').split('/').pop();const chunks=[];let done=0;
    progress={busy:true,id:url.searchParams.get('id'),phase:'upload',name,done:0,total:Number(req.headers['content-length']),elapsed_ms:0,message:''};
    for await(const chunk of req){chunks.push(chunk);done+=chunk.length;progress.done=done;progress.elapsed_ms+=50;}
    await new Promise(r=>setTimeout(r,800));entries.push({name,directory:false,size:done});
    progress={...progress,busy:false,phase:'complete',message:'Upload saved to SD card'};json({saved:true,bytes:done},201);return;
  }
  json({error:'Unexpected test request'},404);
});
(async()=>{
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const origin='http://127.0.0.1:'+server.address().port;
  const browser=await chromium.launch({headless:true,...(process.env.CHROME_PATH?{executablePath:process.env.CHROME_PATH}:{})});
  try {
    const page=await browser.newPage({viewport:{width:1120,height:1000}});
    const errors=[];page.on('pageerror',error=>errors.push(error.message));
    await page.goto(origin);await page.locator('#login').waitFor({state:'visible'});
    await page.locator('#code').fill('2345-6792');await page.getByRole('button',{name:'Open SD card',exact:true}).click();
    await page.locator('#app').waitFor({state:'visible'});await page.waitForFunction(()=>document.fonts.status==='loaded');
    assert(await page.evaluate(()=>document.fonts.check('23px Pixel')&&document.fonts.check('28px PixelTitle')));
    await page.screenshot({path:path.join(output,'browser-dark.png'),fullPage:true});
    await page.locator('#theme').click();assert(await page.locator('html').getAttribute('data-theme')==='light');
    await page.screenshot({path:path.join(output,'browser-light.png'),fullPage:true});
    await page.locator('#fileInput').setInputFiles([{name:'public-browser-upload.txt',mimeType:'text/plain',buffer:Buffer.alloc(65536,0x43)},{name:'public-queued-upload.txt',mimeType:'text/plain',buffer:Buffer.alloc(16384,0x44)}]);
    await page.waitForFunction(()=>document.querySelector('#list').textContent.includes('public-queued-upload.txt'),{timeout:15000});
    assert(entries.length===2);assert(await page.locator('#previous').isDisabled());assert(await page.locator('#next').isDisabled());
    await page.locator('#networkTab').click();await page.locator('#networkPanel').waitFor({state:'visible'});
    await page.locator('#foundNetworks').selectOption('Example LAN');assert(await page.locator('#ssid').inputValue()==='Example LAN');
    await page.locator('#filesTab').click();await page.setViewportSize({width:390,height:844});
    assert(await page.evaluate(()=>document.documentElement.scrollWidth<=window.innerWidth));
    await page.screenshot({path:path.join(output,'browser-mobile-light.png'),fullPage:true});
    assert(errors.length===0,errors.join('\n'));
    fs.writeFileSync(path.join(output,'results.json'),JSON.stringify({result:'PASS',tests:['actual PixelFonts loaded','dark/light desktop and mobile layout','sequential native file uploads','pagination bounds','network selection','no page errors']},null,2));
    console.log('PASS browser themes, actual PixelFonts, mobile layout, upload queue, pagination and network selection');
  }finally{await browser.close();server.close();}
})().catch(error=>{console.error(error.message);server.close();process.exitCode=1;});
