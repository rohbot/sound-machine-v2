#pragma once
#include <Arduino.h>
const char WEB_UI[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="theme-color" content="#111b24"><title>Sound Machine</title>
<style>
:root{color-scheme:dark;font-family:system-ui,sans-serif;background:#111b24;color:#f2f2ec;--glow:#ff4d00}*{box-sizing:border-box}body{margin:0;padding:36px 20px}main{max-width:560px;margin:auto}header{display:flex;justify-content:space-between;align-items:center;gap:16px}h1{font-size:1.6rem;margin:0}h2{font-size:1.15rem;margin:0}p{color:#afbec8;line-height:1.6}.badge{font-size:.75rem;color:#c8d6c7;border:1px solid #42524d;border-radius:30px;padding:6px 10px;white-space:nowrap}.visual{height:150px;display:grid;place-items:center}.orb{height:80px;width:80px;border:8px dotted var(--glow);border-radius:50%;box-shadow:0 0 48px color-mix(in srgb,var(--glow),transparent 70%);transition:opacity .2s}.card{background:#1b2934;border:1px solid #33434e;border-radius:20px;padding:24px;margin:16px 0}.row{display:flex;justify-content:space-between;align-items:center;gap:16px}button{font:inherit;border:0;cursor:pointer;border-radius:12px;padding:12px 20px;background:#d8e8c6;color:#1a291d;font-weight:650}button.secondary{background:#344753;color:#f2f2ec}button:disabled{opacity:.45;cursor:wait}label{display:block;margin:26px 0 10px;color:#c9d4dc}input[type=range]{width:100%;accent-color:#d8e8c6;height:30px;cursor:pointer}select{font:inherit;background:#243643;color:#f2f2ec;border:1px solid #5b6c78;border-radius:10px;padding:10px;width:100%}.hint{font-size:.8rem;margin:2px 0 12px}[hidden]{display:none!important}input[type=color]{height:50px;width:72px;border:1px solid #5b6c78;border-radius:10px;padding:4px;background:#243643;cursor:pointer}output{font-variant-numeric:tabular-nums;color:#f2f2ec}small{color:#afbec8}#status{min-height:1.6em;font-size:.9rem}footer{text-align:center;margin-top:24px;color:#879ba9;font-size:.8rem}:focus-visible{outline:3px solid #eac697;outline-offset:4px}
</style></head><body><main>
<header><h1>Sound Machine</h1><span class="badge" id="connection">Connecting…</span></header>
<p>A little calm, right where you are.</p><div class="visual" aria-hidden="true"><div class="orb" id="orb"></div></div>
<section class="card" aria-labelledby="soundTitle"><div class="row"><div><h2 id="soundTitle">Your soundscape</h2><small>Generated live on your sound machine</small></div><button id="play" disabled aria-pressed="false">Start</button></div>
<label for="mode">Sound</label><select id="mode" disabled><option value="ambient">Ambient soundscape</option><option value="white">White noise</option></select>
<div id="ambientControls">
<label for="palette">Palette</label><select id="palette" disabled><option value="water">Open Water</option><option value="moss">Moss Garden</option><option value="dusk">After Hours</option></select>
<p class="hint">Evolving notes and bass drones, with a different harmony in each palette.</p>
<label for="density" class="row"><span>Density</span><output id="densityValue" for="density">35%</output></label><input type="range" id="density" min="0" max="100" value="35" disabled><p class="hint">Sparse notes to a fuller soundscape</p>
<label for="space" class="row"><span>Space</span><output id="spaceValue" for="space">65%</output></label><input type="range" id="space" min="0" max="100" value="65" disabled><p class="hint">Close and dry to spacious reverb</p>
<label for="warmth" class="row"><span>Warmth</span><output id="warmthValue" for="warmth">65%</output></label><input type="range" id="warmth" min="0" max="100" value="65" disabled><p class="hint">Brighter tones to a softer, darker sound</p>
</div>
<label for="volume" class="row"><span>Volume</span><output id="volumeValue" for="volume">20%</output></label><input type="range" id="volume" min="0" max="100" value="20" disabled></section>
<section class="card" aria-labelledby="lightTitle"><div class="row"><h2 id="lightTitle">Surround lighting</h2><button id="lights" class="secondary" disabled aria-pressed="true">Turn off</button></div>
<div class="row"><label for="colour">Colour</label><input type="color" id="colour" value="#ff4d00" disabled></div>
<label for="brightness" class="row"><span>Brightness</span><output id="brightnessValue" for="brightness">100%</output></label><input type="range" id="brightness" min="0" max="100" value="100" disabled></section>
<p id="status" role="status" aria-live="polite">Connecting to your sound machine…</p><footer>Sound and lighting work independently. No cloud needed.</footer>
</main><script>
'use strict';
const $=id=>document.getElementById(id);
let state=null, pending={}, sending=false, reading=false, timer;
const sliders=['volume','density','space','warmth','brightness'];
const soundFields=['mode','palette','volume','density','space','warmth'];
const controls=['play','lights','colour','brightness',...soundFields];
function preview(){
 for(const id of sliders) $(id+'Value').textContent=$(id).value+'%';
 $('ambientControls').hidden=$('mode').value!=='ambient';
 document.documentElement.style.setProperty('--glow',$('colour').value);
 $('orb').style.opacity=state&&state.lights?Math.max(.08,$('brightness').value/100):.06;
}
function render(s){
 state=s;
 for(const id of [...soundFields,'brightness','colour']){
  if(document.activeElement!==$(id)&&!(id in pending)) $(id).value=s[id];
 }
 $('play').textContent=s.playing?'Stop':'Start';
 $('play').setAttribute('aria-pressed',s.playing);
 $('lights').textContent=s.lights?'Turn off':'Turn on';
 $('lights').setAttribute('aria-pressed',s.lights);
 controls.forEach(id=>$(id).disabled=false);
 $('play').disabled=!s.audioReady;
 for(const id of soundFields) $(id).disabled=!s.audioReady;
 $('connection').textContent='Connected';
 $('status').textContent=s.audioReady?'Settings are saved automatically.':'Audio unavailable. Check the USB serial log and restart the board.';
 preview();
}
async function api(method='GET',data){
 const response=await fetch('/api/state',{method,cache:'no-store',signal:AbortSignal.timeout(6000),
  ...(data?{headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)}:{})});
 if(!response.ok)throw new Error(await response.text());
 return response.json();
}
function failed(error){
 $('connection').textContent='Disconnected';
 $('status').textContent='Could not update the board: '+error.message+'. Reconnecting…';
 controls.forEach(id=>$(id).disabled=true);
}
function queue(key,value){
 pending[key]=String(value);
 clearTimeout(timer);
 timer=setTimeout(flush,100);
}
async function flush(){
 if(sending||reading||!Object.keys(pending).length)return;
 const data=pending;pending={};sending=true;
 try{render(await api('POST',data));}
 catch(error){failed(error);pending={};}
 finally{sending=false;if(Object.keys(pending).length)flush();}
}
$('play').onclick=()=>queue('playing',!state.playing);
$('lights').onclick=()=>queue('lights',!state.lights);
for(const id of [...sliders,'colour']){
 $(id).oninput=()=>{preview();queue(id,$(id).value);};
 $(id).onchange=()=>{queue(id,$(id).value);clearTimeout(timer);flush();};
}
for(const id of ['mode','palette']) $(id).onchange=()=>{preview();queue(id,$(id).value);clearTimeout(timer);flush();};
async function refresh(){
 if(sending||reading||Object.keys(pending).length)return;
 reading=true;
 try{render(await api());}catch(error){failed(error);}
 finally{reading=false;flush();}
}
refresh();setInterval(refresh,3000);
</script></body></html>)HTML";
