const fs = require('node:fs');
const path = require('node:path');
const project = path.resolve(__dirname, '../..');
const output = process.env.STATS_TEST_OUTPUT || path.join(project, '.test-output');
const assert = require('node:assert/strict');
const {chromium} = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
(async()=>{
  const header=fs.readFileSync(path.join(project,'src/stats/dashboard_page.h'),'utf8');
  const html=header.split('R"ADVANCEDHTML(')[1].split(')ADVANCEDHTML"')[0];
  const fixture=JSON.parse(fs.readFileSync(path.join(__dirname,'dashboard_fixture.json'),'utf8'));
  const browser=await chromium.launch({headless:true,executablePath:process.env.CHROME_EXECUTABLE || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'});
  const context=await browser.newContext({viewport:{width:1280,height:900}});
  const page=await context.newPage();
  let mode='live',requests=0,methodErrors=0;
  await page.addInitScript(()=>{window.testDelays=[];const original=window.setTimeout;window.setTimeout=function(fn,delay,...rest){if(fn?.name==='refresh')window.testDelays.push(delay);return original(fn,delay,...rest);};});
  await page.route('http://miner.test/**',async route=>{
    const request=route.request();
    if(request.method()!=='GET')methodErrors++;
    if(request.url()==='http://miner.test/')return route.fulfill({contentType:'text/html',body:html});
    if(request.url().endsWith('/api/advanced-stats')){
      requests++;
      if(mode==='unavailable')return route.fulfill({status:503,contentType:'application/json',body:'{}'});
      if(mode==='delay')await new Promise(resolve=>setTimeout(resolve,180));
      let body=structuredClone(fixture);
      if(mode==='invalid')body.schema=2;
      if(mode==='stale')body.sample_age_ms='30000';
      if(mode==='reboot')body.boot_id='43';
      if(mode==='save-failed'){body.save_attempted=true;body.last_save_ok=false;}
      return route.fulfill({contentType:'application/json',body:JSON.stringify(body)});
    }
    return route.abort();
  });
  try {
    await page.goto('http://miner.test/');
    await page.waitForFunction(()=>document.getElementById('status').textContent.startsWith('Live'));
    assert.equal(await page.locator('#hashes').textContent(),'9.007.199.254.740.993');
    assert.equal(await page.locator('#accepted').textContent(),'1.024');
    assert.equal(await page.locator('#pool').textContent(),'Verbunden');
    assert.equal(await page.locator('#sessionSuccess').textContent(),'96,00 %');
    assert.equal(await page.locator('#totalSuccess').textContent(),'98,84 %');
    for(const value of [null,0,100]){
      const expected=value===null?'—':value.toLocaleString('de-DE',{minimumFractionDigits:2,maximumFractionDigits:2})+' %';
      await page.evaluate(({base,value})=>{base.session.success_percent=value;base.lifetime.success_percent=value;window.AdvancedStats.render(window.AdvancedStats.parseSnapshot(base));},{base:fixture,value});
      assert.equal(await page.locator('#sessionSuccess').textContent(),expected);
      assert.equal(await page.locator('#totalSuccess').textContent(),expected);
    }
    for(const value of [-1,101,'100'])await assert.rejects(page.evaluate(({base,value})=>{base.session.success_percent=value;return window.AdvancedStats.parseSnapshot(base);},{base:fixture,value}));
    await page.evaluate(base=>window.AdvancedStats.render(base),fixture);

    assert.equal(await page.locator('#persistence').textContent(),'Letzter Speicherpunkt erfolgreich');
    assert.equal(await page.evaluate(()=>window.testDelays.at(-1)),10000);
    const invalid=[{schema:2},{hashrate_hs:-1},{pool_connected:'true'}, {sample_age_ms:'-1'}];
    for(const patch of invalid)await assert.rejects(page.evaluate(({base,patch})=>window.AdvancedStats.parseSnapshot({...base,...patch}),{base:fixture,patch}));
    await assert.rejects(page.evaluate(base=>{base.lifetime.hashes='18446744073709551616';return window.AdvancedStats.parseSnapshot(base);},structuredClone(fixture)));
    await assert.rejects(page.evaluate(base=>{base.lifetime.accepted='<img src=x onerror=alert(1)>';return window.AdvancedStats.parseSnapshot(base);},structuredClone(fixture)));
    mode='invalid';await page.evaluate(()=>window.AdvancedStats.refresh());
    assert.match(await page.locator('#status').textContent(),/Keine aktuellen Daten/);
    assert.equal(await page.locator('#hashes').textContent(),'9.007.199.254.740.993');
    mode='live';await page.evaluate(()=>window.AdvancedStats.refresh());
    mode='unavailable';await page.evaluate(()=>window.AdvancedStats.refresh());
    assert.match(await page.locator('#status').textContent(),/veraltet/);
    assert.equal(await page.evaluate(()=>window.testDelays.at(-1)),20000);
    mode='stale';await page.evaluate(()=>window.AdvancedStats.refresh());
    assert.equal(await page.locator('#status').textContent(),'Veraltete Messwerte');
    mode='reboot';await page.evaluate(()=>window.AdvancedStats.refresh());
    assert.equal(await page.locator('#status').textContent(),'Gerät neu gestartet · live');
    mode='save-failed';await page.evaluate(()=>window.AdvancedStats.refresh());
    assert.match(await page.locator('#persistence').textContent(),/Fehlgeschlagen/);
    assert.match(await page.locator('#status').textContent(),/Speicherung fehlgeschlagen/);
    // No overlapping requests, even when callers ask for multiple refreshes.
    mode='delay';const before=requests;
    await page.evaluate(()=>Promise.all([window.AdvancedStats.refresh(),window.AdvancedStats.refresh(),window.AdvancedStats.refresh()]));
    assert.equal(requests,before+1);
    // Hidden page: cancel polling; returning to it resumes acquisition.
    await page.evaluate(()=>{Object.defineProperty(document,'hidden',{value:true,configurable:true});document.dispatchEvent(new Event('visibilitychange'));});
    const hidden=requests;await page.evaluate(()=>window.AdvancedStats.refresh());assert.equal(requests,hidden);
    mode='live';await page.evaluate(()=>{Object.defineProperty(document,'hidden',{value:false,configurable:true});document.dispatchEvent(new Event('visibilitychange'));});
    await page.waitForFunction(()=>document.getElementById('status').textContent.startsWith('Live'));
    assert.equal(methodErrors,0);
    // Screenshots use explicit simulated values from the C++ serializer.
    await page.evaluate(()=>{document.querySelector('.tag').textContent='VORSCHAU · SIMULIERTE TESTDATEN';document.title='Vorschau mit Testdaten';});
    fs.mkdirSync(output,{recursive:true});
    await page.screenshot({path:path.join(output,'desktop.png'),fullPage:true});
    await page.setViewportSize({width:390,height:844});
    assert.ok(await page.evaluate(()=>document.documentElement.scrollWidth<=window.innerWidth));
    await page.screenshot({path:path.join(output,'mobile.png'),fullPage:true});
    console.log('Dashboard: rendering, 64-bit counters, invalid inputs, stale data, persistence failure, reconnect, reboot, single-flight, hidden-page polling and mobile layout passed.');
  } finally {await context.close();await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
