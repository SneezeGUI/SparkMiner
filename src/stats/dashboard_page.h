#pragma once
#include <pgmspace.h>
static const char ADVANCED_DASHBOARD[] PROGMEM = R"ADVANCEDHTML(<!doctype html>
<html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Advanced Statistics · SparkMiner</title><style>
:root{color-scheme:dark;font-family:system-ui,sans-serif;background:#101820;color:#edf3f7}*{box-sizing:border-box}body{margin:0 auto;padding:28px 18px;max-width:980px}header{display:flex;justify-content:space-between;gap:12px;align-items:center;flex-wrap:wrap}h1{font-size:clamp(1.5rem,4vw,2.1rem);margin:.2em 0}p{color:#b9c8d3;line-height:1.6}.tag{color:#67dfc0;font-size:.8rem;letter-spacing:.1em}#status{padding:10px 14px;border:1px solid #465d70;border-radius:24px;font-size:.9rem}.bad{color:#ffd08c!important;border-color:#d39a4d!important}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:14px;margin:24px 0}.card,section{padding:20px;border:1px solid #304454;border-radius:14px;background:#172631}.label{font-size:.9rem;color:#aec2d1}.value{font-size:1.7rem;margin:10px 0 2px;font-variant-numeric:tabular-nums;overflow-wrap:anywhere}.sub{font-size:.8rem;color:#b3c6d5}section{margin:14px 0}h2{margin-top:0;font-size:1.1rem}dl{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-bottom:0}dt{color:#b3c6d5}dd{margin:0;text-align:right;overflow-wrap:anywhere}footer{font-size:.82rem;margin-top:24px;color:#b3c6d5}.sr{position:absolute;width:1px;height:1px;overflow:hidden;clip:rect(0,0,0,0)}
@media(max-width:520px){dl{grid-template-columns:1fr;gap:6px}dt{font-size:.85rem}dd{text-align:left;margin-bottom:12px}}
</style></head><body><header><div><div class="tag">SPARKMINER · LOKAL</div><h1>Advanced Statistics</h1></div><span id="status" role="status">Verbindung wird aufgebaut …</span></header><p><a href="/settings" style="color:#67dfc0">Einstellungen</a></p>
<p>Gesamtwerte und aktuelle Sitzung stammen aus derselben Momentaufnahme wie die Anzeige am Miner.</p>
<div class="grid"><div class="card"><div class="label">Aktuelle Hashrate</div><div class="value" id="hashrate">—</div><div class="sub">H/s · geglättet</div></div><div class="card"><div class="label">Akzeptierte Shares · gesamt</div><div class="value" id="accepted">—</div><div class="sub" id="sessionAccepted">Sitzung: —</div></div><div class="card"><div class="label">Abgelehnte Shares · gesamt</div><div class="value" id="rejected">—</div><div class="sub" id="sessionRejected">Sitzung: —</div></div><div class="card"><div class="label">Poolverbindung</div><div class="value" id="pool">—</div><div class="sub" id="wifi">WLAN: —</div></div></div>
<section><h2>Gesamtstatistik</h2><dl><dt>Erfolgsquote</dt><dd id="totalSuccess">—</dd><dt>Berechnete Hashes</dt><dd id="hashes">—</dd><dt>Übermittelte Shares</dt><dd id="submitted">—</dd><dt>Beste Difficulty</dt><dd id="difficulty">—</dd><dt>Laufzeit</dt><dd id="lifetimeUptime">—</dd><dt>Gefundene Blockkandidaten</dt><dd id="blocks">—</dd></dl></section>
<section><h2>Aktuelle Sitzung</h2><dl><dt>Erfolgsquote</dt><dd id="sessionSuccess">—</dd><dt>Berechnete Hashes</dt><dd id="sessionHashes">—</dd><dt>Laufzeit</dt><dd id="sessionUptime">—</dd><dt>Statistikspeicherung</dt><dd id="persistence">—</dd><dt>Messwertalter beim Abruf</dt><dd id="age">—</dd></dl></section>
<footer>Erfolgsquote = akzeptierte Shares ÷ beantwortete Shares. Noch unbeantwortete Übermittlungen zählen nicht als Fehler.<br>Aktualisierung alle zehn Sekunden bei sichtbarer Seite. Bei Verbindungsfehlern bleiben die letzten Werte sichtbar und werden als veraltet markiert. Bei Stromausfall können Werte seit der letzten erfolgreichen Speicherung verloren gehen. E-Mail-Berichte sind noch nicht verfügbar.</footer>
<script>
'use strict';
const fields=['hashes','submitted','accepted','rejected','blocks','uptime_s'];
function uint(value, max=18446744073709551615n){
  if(typeof value!=='string'||!/^(0|[1-9][0-9]{0,19})$/.test(value)||BigInt(value)>max)throw new Error('Ungültiger Zähler');
  return BigInt(value);
}
function parseSnapshot(s){
  if(!s||s.schema!==1||!Number.isFinite(s.hashrate_hs)||s.hashrate_hs<0)throw new Error('Ungültige Statistik');
  uint(s.sequence);uint(s.boot_id,4294967295n);uint(s.sample_age_ms);
  for(const key of ['pool_connected','wifi_connected','save_attempted','last_save_ok','counter_limit_reached'])if(typeof s[key]!=='boolean')throw new Error('Ungültiger Status');
  for(const group of ['session','lifetime']){
    if(!s[group])throw new Error('Fehlende Zähler');
    const percent=s[group].success_percent;
    if(percent!==null&&(!Number.isFinite(percent)||percent<0||percent>100))throw new Error('Ungültige Erfolgsquote');
    for(const key of fields)uint(s[group][key],key==='hashes'?18446744073709551615n:4294967295n);
  }
  if(!Number.isFinite(s.lifetime.best_difficulty)||s.lifetime.best_difficulty<0)throw new Error('Ungültige Difficulty');
  return s;
}
function number(s){return BigInt(s).toLocaleString('de-DE');}
function duration(s){const n=BigInt(s);return `${n/86400n} T ${n%86400n/3600n} Std ${n%3600n/60n} Min ${n%60n} Sek`;}
function text(id,value){document.getElementById(id).textContent=value;}
function formatSuccess(value){return value===null?'—':value.toLocaleString('de-DE',{minimumFractionDigits:2,maximumFractionDigits:2})+' %';}
function render(s){
  text('totalSuccess',formatSuccess(s.lifetime.success_percent));
  text('sessionSuccess',formatSuccess(s.session.success_percent));
  text('hashrate',s.hashrate_hs.toLocaleString('de-DE',{maximumFractionDigits:0}));
  for(const key of ['accepted','rejected','hashes','submitted','blocks'])text(key,number(s.lifetime[key]));
  text('sessionAccepted','Sitzung: '+number(s.session.accepted));text('sessionRejected','Sitzung: '+number(s.session.rejected));
  text('sessionHashes',number(s.session.hashes));text('sessionUptime',duration(s.session.uptime_s));text('lifetimeUptime',duration(s.lifetime.uptime_s));
  text('pool',s.pool_connected?'Verbunden':'Getrennt');text('wifi','WLAN: '+(s.wifi_connected?'verbunden':'getrennt'));
  text('difficulty',s.lifetime.best_difficulty.toLocaleString('de-DE',{maximumSignificantDigits:8}));
  text('persistence',!s.save_attempted?'Noch kein Speicherpunkt':s.last_save_ok?'Letzter Speicherpunkt erfolgreich':'Fehlgeschlagen · neuer Versuch folgt');
  text('age',(Number(s.sample_age_ms)/1000).toLocaleString('de-DE',{maximumFractionDigits:1})+' Sek');
}
let timer=null,controller=null,active=false,lastBoot=null,failures=0;
function status(message,bad=false){text('status',message);document.getElementById('status').classList.toggle('bad',bad);}
async function refresh(){
  if(active||document.hidden)return;
  active=true;controller=new AbortController();
  const timeout=setTimeout(()=>controller?.abort(),5000);
  try{
    const response=await fetch('/api/advanced-stats',{cache:'no-store',signal:controller.signal});
    if(!response.ok)throw new Error('Status nicht verfügbar');
    const s=parseSnapshot(await response.json());render(s);
    const reboot=lastBoot!==null&&lastBoot!==s.boot_id;lastBoot=s.boot_id;failures=0;
    if(uint(s.sample_age_ms)>15000n)status('Veraltete Messwerte',true);
    else if(s.counter_limit_reached)status('Zählergrenze erreicht',true);
    else if(s.save_attempted&&!s.last_save_ok)status('Live · Speicherung fehlgeschlagen',true);
    else status(reboot?'Gerät neu gestartet · live':'Live · '+new Date().toLocaleTimeString('de-DE'));
  }catch(e){failures++;status('Keine aktuellen Daten · letzte Werte veraltet',true);}
  finally{
    clearTimeout(timeout);controller=null;active=false;clearTimeout(timer);
    if(!document.hidden)timer=setTimeout(refresh,Math.min(60000,10000*2**Math.min(failures,3)));
  }
}
document.addEventListener('visibilitychange',()=>{clearTimeout(timer);if(document.hidden)controller?.abort();else refresh();});
window.AdvancedStats={parseSnapshot,render,refresh};
refresh();
</script></body></html>)ADVANCEDHTML";
