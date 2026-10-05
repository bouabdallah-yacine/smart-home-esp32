// Web page served directly by the ESP32 (no external server, no cloud)
#pragma once
static const char WEB_PAGE[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Smart Home</title>
<style>
:root{--bg:#f3f1ec;--card:#fff;--ink:#1f2a2e;--mut:#6b7679;--line:#e2ded6;--acc:#2f6f63;--warn:#c27a00;--crit:#c0392b;--on:#2f6f63}
@media(prefers-color-scheme:dark){:root:not([data-theme="light"]){--bg:#141a1c;--card:#1d2528;--ink:#e6ecea;--mut:#93a09e;--line:#2b3538;--acc:#5fb8a6;--warn:#f0a531;--crit:#ef6a58;--on:#5fb8a6;color-scheme:dark}}
:root[data-theme="dark"]{--bg:#141a1c;--card:#1d2528;--ink:#e6ecea;--mut:#93a09e;--line:#2b3538;--acc:#5fb8a6;--warn:#f0a531;--crit:#ef6a58;--on:#5fb8a6;color-scheme:dark}
:root[data-theme="light"]{color-scheme:light}
header{display:flex;justify-content:space-between;align-items:flex-start;gap:10px}#theme{min-width:42px}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif;padding:16px}
main{max-width:860px;margin:0 auto;display:grid;gap:14px}
h1{font-size:22px;margin:0}header p{margin:2px 0 0;color:var(--mut);font-size:13px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:12px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:14px}
.lab{font-size:12px;color:var(--mut);text-transform:uppercase;letter-spacing:.05em}
.sub{font-size:12px;color:var(--mut)}.val{font-size:26px;font-weight:650;font-variant-numeric:tabular-nums}.val small{font-size:13px;color:var(--mut);font-weight:400}
.dev{display:flex;justify-content:space-between;padding:6px 0;border-bottom:1px solid var(--line)}.dev:last-child{border:0}
.pill{font-size:12px;font-weight:600;padding:2px 9px;border-radius:99px;background:var(--line);color:var(--mut)}
.pill.on{background:var(--on);color:var(--card)}.pill.crit{background:var(--crit);color:#fff}
.row{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin-top:8px}
button{font:inherit;border:1px solid var(--line);background:var(--bg);color:var(--ink);padding:7px 12px;border-radius:8px;cursor:pointer}
button.sel{background:var(--acc);color:var(--card);border-color:var(--acc)}button:focus-visible,input:focus-visible{outline:2px solid var(--acc)}
input{font:inherit;width:90px;padding:7px;border:1px solid var(--line);border-radius:8px;background:var(--bg);color:var(--ink)}
#banner{display:none;padding:12px 14px;border-radius:10px;background:var(--crit);color:#fff;font-weight:600}
ol{margin:0;padding:0;list-style:none;font-size:13px}ol li{padding:4px 0;border-bottom:1px solid var(--line)}ol span{color:var(--mut);font-variant-numeric:tabular-nums;margin-right:8px}
</style></head><body><main>
<header><div><h1>Smart Home</h1><p>Served by the ESP32 · works without Internet</p></div><button id="theme" aria-label="Toggle theme">◐</button></header>
<div id="banner"></div>
<section class="grid">
 <div class="card"><div class="lab">Temperature</div><div class="val" id="t">–</div><div class="sub" id="h"></div></div>
 <div class="card"><div class="lab">Light level</div><div class="val" id="l">–</div></div>
 <div class="card"><div class="lab">Gas</div><div class="val" id="g">–</div></div>
 <div class="card"><div class="lab">Power</div><div class="val" id="p">–</div><div class="sub" id="e"></div></div>
</section>
<section class="grid">
 <div class="card"><div class="lab">Devices</div>
  <div class="dev">Living-room lamp <span class="pill" id="d_l"></span></div>
  <div class="dev">Heating <span class="pill" id="d_h"></span></div>
  <div class="dev">Ventilation <span class="pill" id="d_f"></span></div>
  <div class="dev">Blinds <span class="pill" id="d_b"></span></div>
  <div class="dev">Door <span class="pill" id="d_d"></span></div>
 </div>
 <div class="card"><div class="lab">Mode</div>
  <div class="row" id="modes"><button data-m="home">Home</button><button data-m="night">Night</button><button data-m="away">Away</button></div>
  <div class="lab" style="margin-top:14px">Heating setpoint</div>
  <div class="row"><button onclick="cmd('sp','-0.5')" aria-label="Lower setpoint">−</button><b id="sp" style="min-width:60px;text-align:center"></b><button onclick="cmd('sp','0.5')" aria-label="Raise setpoint">+</button></div>
  <div class="lab" style="margin-top:14px">Lamp</div>
  <div class="row"><button onclick="cmd('light','on')">On</button><button onclick="cmd('light','off')">Off</button><button onclick="cmd('light','auto')">Auto</button></div>
 </div>
 <div class="card"><div class="lab">Alarm</div><div class="val" id="a" style="font-size:20px">–</div>
  <div class="row"><button onclick="cmd('arm','')">Arm</button></div>
  <div class="row"><input id="code" type="password" inputmode="numeric" maxlength="4" placeholder="code" aria-label="Alarm code"><button onclick="cmd('disarm',document.getElementById('code').value);document.getElementById('code').value=''">Disarm</button></div>
 </div>
</section>
<section class="card"><div class="lab">Event log</div><ol id="log"></ol></section>
</main><script>
const $=id=>document.getElementById(id);
(function(){const r=document.documentElement;let t=null;try{t=localStorage.getItem('theme')}catch(e){}
 if(t)r.dataset.theme=t;
 const cur=()=>r.dataset.theme||(matchMedia('(prefers-color-scheme: dark)').matches?'dark':'light');
 const lab=()=>{$('theme').textContent=cur()=='dark'?'☀':'☾';$('theme').title=cur()=='dark'?'Switch to light mode':'Switch to dark mode'};
 $('theme').onclick=()=>{r.dataset.theme=cur()=='dark'?'light':'dark';try{localStorage.setItem('theme',r.dataset.theme)}catch(e){}lab()};lab()})();
function pill(id,on,txt,crit){const e=$(id);e.textContent=txt;e.className='pill'+(on?(crit?' crit':' on'):'')}
async function cmd(c,v){try{await fetch('/api/cmd?c='+c+'&v='+encodeURIComponent(v));}catch(e){}refresh()}
document.querySelectorAll('#modes button').forEach(b=>b.onclick=()=>cmd('mode',b.dataset.m));
async function refresh(){try{
 const s=await (await fetch('/api/state')).json();
 $('t').innerHTML=s.temp.toFixed(1)+'<small> °C</small>';$('h').textContent='humidity '+s.hum.toFixed(0)+' %';
 $('l').innerHTML=Math.round(s.lux)+'<small> lux</small>';$('g').innerHTML=Math.round(s.gas)+'<small> ppm</small>';
 $('p').innerHTML=Math.round(s.power)+'<small> W</small>';$('e').textContent=s.energy.toFixed(1)+' Wh since startup';
 pill('d_l',s.light>0,s.light>0?s.light+' %':'off');pill('d_h',s.heater,s.heater?'on':'off');
 pill('d_f',s.fan,s.fan?'on':'off');pill('d_b',s.blinds>0,s.blinds>=90?'closed':s.blinds>0?'half-closed':'open');
 pill('d_d',s.door,s.door?'open':'closed',true);
 document.querySelectorAll('#modes button').forEach(b=>b.className=b.dataset.m==s.mode?'sel':'');
 $('sp').textContent=s.sp.toFixed(1)+' °C';$('a').textContent=s.alarm+(s.locked?' (keypad locked)':'');
 const bn=$('banner');bn.style.display=(s.gasAlarm||s.siren)?'block':'none';
 bn.textContent=s.gasAlarm?'⚠ Gas leak: forced ventilation, heating off':'⚠ Intrusion: siren triggered';
 $('log').innerHTML=s.log.map(e=>'<li><span>'+e.t+' s</span>'+e.m+'</li>').join('');
}catch(e){}}
refresh();setInterval(refresh,1000);
</script></body></html>)HTML";
