// Exercise the actual embedded JavaScript with a small DOM and device API stub.
// Run: node --test tests/web_ui.test.mjs
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
const source = readFileSync(new URL('../include/web_ui.h', import.meta.url), 'utf8').match(/<script>([\s\S]*?)<\/script>/)[1];
const initial = {playing:false,volume:20,lights:true,brightness:100,colour:'#ff4d00',audioReady:true,mode:'ambient',palette:'water',density:35,space:65,warmth:65};
async function ui() {
  const elements = new Map();
  const document = {activeElement:null, documentElement:{style:{setProperty(){}}}, getElementById(id){
    if(!elements.has(id)) elements.set(id,{value:'',disabled:true,style:{},setAttribute(k,v){this[k]=v;}});
    return elements.get(id);
  }};
  let device={...initial}, fail=false, hold=false, release;
  const calls=[], timers=new Map();let nextTimer=0;
  const context=vm.createContext({document,URLSearchParams,AbortSignal,
    setTimeout(fn){timers.set(++nextTimer,fn);return nextTimer;},clearTimeout(id){timers.delete(id);},setInterval(){},
    async fetch(url,options){
      calls.push({url,...options});
      if(hold) await new Promise(resolve=>{release=resolve;});
      if(fail) throw new Error('network unavailable');
      if(options.method==='POST') for(const [key,value] of options.body){
        device[key]=['playing','lights'].includes(key)?value==='true':['volume','brightness','density','space','warmth'].includes(key)?Number(value):value;
      }
      return {ok:true,json:async()=>({...device})};
    }
  });
  vm.runInContext(source,context);
  const settle=()=>new Promise(resolve=>setImmediate(resolve));
  await settle();
  return {elements,calls,document,context,settle,
    async runTimers(){const pending=[...timers.values()];timers.clear();pending.forEach(fn=>fn());await settle();},
    setFail(v){fail=v;},setReady(v){device.audioReady=v;},hold(){hold=true;},release(){hold=false;release();},
    refresh(){return vm.runInContext('refresh()',context);}
  };
}
test('loads actual state and independently controls sound and lighting',async()=>{
  const u=await ui();const el=id=>u.elements.get(id);
  assert.equal(el('connection').textContent,'Connected');
  assert.equal(el('play').disabled,false);
  el('play').onclick();await u.runTimers();
  assert.equal(el('play').textContent,'Stop');
  el('lights').onclick();await u.runTimers();
  assert.equal(el('lights').textContent,'Turn on');
  assert.equal(el('play').textContent,'Stop');
});
test('coalesces slider updates and sends colour without losing the hash',async()=>{
  const u=await ui();const el=id=>u.elements.get(id);
  el('volume').value='40';el('volume').oninput();
  el('volume').value='63';el('volume').oninput();
  el('brightness').value='12';el('brightness').oninput();
  el('colour').value='#123abc';el('colour').oninput();
  assert.equal(el('volumeValue').textContent,'63%');
  await u.runTimers();
  const writes=u.calls.filter(c=>c.method==='POST');
  assert.equal(writes.length,1);
  assert.deepEqual(Object.fromEntries(writes[0].body),{volume:'63',brightness:'12',colour:'#123abc'});
});
test('serializes changes arriving during an in-flight request',async()=>{
  const u=await ui();const el=id=>u.elements.get(id);
  u.hold();el('volume').value='30';el('volume').onchange();await u.settle();
  el('volume').value='80';el('volume').oninput();await u.runTimers();
  assert.equal(u.calls.filter(c=>c.method==='POST').length,1);
  u.release();await u.settle();await u.settle();
  const writes=u.calls.filter(c=>c.method==='POST');
  assert.equal(writes.length,2);assert.equal(writes[1].body.get('volume'),'80');
  assert.equal(el('volume').value,80);
});
test('disables controls on a network failure and restores them on reconnect',async()=>{
  const u=await ui();u.setFail(true);await u.refresh();
  assert.equal(u.elements.get('connection').textContent,'Disconnected');
  assert.equal(u.elements.get('play').disabled,true);
  u.setFail(false);await u.refresh();
  assert.equal(u.elements.get('play').disabled,false);
  assert.equal(u.elements.get('connection').textContent,'Connected');
});
test('keeps lighting usable when the audio hardware reports a failure',async()=>{
  const u=await ui();u.setReady(false);await u.refresh();
  assert.equal(u.elements.get('play').disabled,true);
  assert.equal(u.elements.get('lights').disabled,false);
  assert.match(u.elements.get('status').textContent,/Audio unavailable/);
});

test('ambient palette and texture controls send their settings with independent lighting',async()=>{
  const u=await ui();const el=id=>u.elements.get(id);
  assert.equal(el('mode').value,'ambient');
  assert.equal(el('ambientControls').hidden,false);
  el('palette').value='moss';el('palette').onchange();await u.settle();
  for(const [id,value] of [['density','75'],['space','90'],['warmth','15']]){
    el(id).value=value;el(id).oninput();
  }
  await u.runTimers();
  const writes=u.calls.filter(c=>c.method==='POST');
  assert.equal(writes[0].body.get('palette'),'moss');
  assert.deepEqual(Object.fromEntries(writes[1].body),{density:'75',space:'90',warmth:'15'});
  assert.equal(el('lights').getAttribute?.('aria-pressed') ?? el('lights')['aria-pressed'],true);
});
test('white noise hides ambient controls and changing back restores them',async()=>{
  const u=await ui();const el=id=>u.elements.get(id);
  el('mode').value='white';el('mode').onchange();await u.settle();
  assert.equal(el('ambientControls').hidden,true);
  assert.equal(el('volume').disabled,false);
  el('mode').value='ambient';el('mode').onchange();await u.settle();
  assert.equal(el('ambientControls').hidden,false);
  assert.equal(el('palette').value,'water');
});
