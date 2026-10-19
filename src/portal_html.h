#pragma once
#include <Arduino.h>
// Served by the CYD both as the setup hotspot page and as the web admin on your network.
// Fully offline (no web fonts / CDNs): the setup hotspot has no internet.
static const char PORTAL_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Neco // CYD</title>
<style>
:root{--bg:#020603;--panel:#0e160f;--line:#46563c;--green:#97f8a6;--hot:#d5ffd9;--dim:#5c9465;--amber:#e8c547;--rust:#983724}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--green);font:16px/1.45 ui-monospace,"DejaVu Sans Mono",Menlo,monospace;
 background-image:repeating-linear-gradient(0deg,rgba(151,248,166,.035) 0 1px,transparent 1px 3px)}
main{max-width:560px;margin:0 auto;padding:18px 16px 60px}
h1{color:var(--hot);font-size:22px;letter-spacing:.08em;margin:6px 0 2px;text-shadow:0 0 8px rgba(151,248,166,.35)}
.sub{color:var(--dim);margin:0 0 18px}
section{border:1px solid var(--line);background:var(--panel);padding:14px;margin:14px 0;position:relative}
section:before,section:after{content:"";position:absolute;width:10px;height:10px;border:solid var(--amber)}
section:before{left:-1px;top:-1px;border-width:1px 0 0 1px}section:after{right:-1px;bottom:-1px;border-width:0 1px 1px 0}
h2{font-size:14px;color:var(--amber);letter-spacing:.12em;margin:0 0 10px;text-transform:uppercase}
label{display:block;color:var(--dim);font-size:13px;margin:10px 0 4px}
textarea{width:100%;font:13px/1.4 ui-monospace,monospace;background:var(--bg);color:var(--hot);border:1px solid var(--line);padding:10px;resize:vertical}
input,select,button{width:100%;font:inherit;background:var(--bg);color:var(--hot);border:1px solid var(--line);padding:10px;border-radius:0}
input:focus,select:focus{outline:1px solid var(--amber);border-color:var(--amber)}
button{cursor:pointer;color:var(--green);margin-top:14px;letter-spacing:.08em}
button.go{border-color:var(--green);color:var(--bg);background:var(--green);font-weight:bold}
button.warn{color:var(--rust);border-color:var(--rust)}
.row{display:flex;gap:8px}.row>*{flex:1}
.hint{color:var(--dim);font-size:12px;margin-top:4px}
#msg{min-height:1.4em;color:var(--amber);margin-top:10px}
.mem{border-top:1px dashed var(--line);padding:8px 0;display:flex;gap:8px;align-items:flex-start}
.mem b{color:var(--amber);font-weight:normal;font-size:12px;white-space:nowrap}.mem span{flex:1}
.mem button{width:auto;margin:0;padding:2px 8px;font-size:12px}
a{color:var(--green)}
.admin{display:none}
</style></head><body><main>
<h1>NECO // CYD</h1>
<p class="sub" id="sub">setup</p>

<section><h2>wifi</h2>
<label>network</label>
<select id="net"></select>
<input id="ssid" placeholder="or type a network name" style="margin-top:6px">
<label>password</label><input id="pass" type="password" autocomplete="off">
<div class="hint" id="saved"></div>
</section>

<section><h2>brain</h2>
<label>deepseek api key</label><input id="key" type="password" autocomplete="off" placeholder="sk-...">
<div class="hint" id="keyhint">from platform.deepseek.com. stored on the board, never shown again.</div>
<label>model</label><input id="model" placeholder="deepseek-flash">
<div class="row"><div><label>she calls you</label><input id="name" placeholder="Echo"></div>
<div><label>cat</label><select id="cat"><option value="0">Neco</option><option value="1">Coneco</option><option value="2">Sakamoto</option></select></div></div>
<div class="row"><div><label>utc offset</label><input id="tz" type="number" min="-12" max="14"></div>
<div><label>idle thoughts</label><select id="idle"><option value="0">off</option><option value="15">every 15 min</option>
<option value="30">every 30 min</option><option value="60">every hour</option></select></div></div>
<button class="go" id="save">SAVE</button>
<div id="msg"></div>
</section>

<section><h2>about this body</h2>
<div class="hint">facts she knows about this device and its history. add anything true you want her to know.
facts work better than personality instructions.</div>
<textarea id="body" rows="12"></textarea>
<button id="bodysave">SAVE NOTES</button>
<div id="bodymsg" class="hint"></div>
</section>

<section><h2>reflections</h2>
<div class="hint" id="memcount"></div>
<div id="mems" class="admin"></div>
<label>import from neco-ai (<code>python3 scripts/neco-memory.py export neco-memory.json</code>)</label>
<input type="file" id="imp" accept=".json,application/json">
<button id="impbtn">IMPORT</button>
<div class="row admin" id="memtools"><button onclick="location='/api/memory?download=1'">EXPORT</button>
<button class="warn" id="clear">FORGET ALL</button></div>
</section>
<p class="hint">neco-cyd · an experiment in a cat living in a cheap screen</p>
</main>
<script>
const $=id=>document.getElementById(id);let C={};
function say(t){$('msg').textContent=t}
async function load(){
 C=await (await fetch('/api/config')).json();
 $('sub').textContent=C.mode=='setup'?'setup hotspot. fill this in, she restarts and joins your wifi.':'web admin on '+C.ip;
 const n=$('net');n.innerHTML='<option value="">-- pick a network --</option>'+C.nets.map(x=>`<option>${x.ssid.replace(/</g,'&lt;')}</option>`).join('');
 $('saved').textContent=C.saved.length?'saved: '+C.saved.join(', '):'no saved networks yet';
 $('model').value=C.model;$('name').value=C.name;$('cat').value=C.cat;$('tz').value=C.tz;$('idle').value=C.idle;
 if(C.keyHint)$('keyhint').textContent='key saved (…'+C.keyHint+'). leave empty to keep it.';
 $('memcount').textContent=C.mem+' reflections on the board.';
 if(C.mode=='admin'){document.querySelectorAll('.admin').forEach(e=>e.style.display='');loadMem()}
}
async function loadMem(){
 const d=await (await fetch('/api/memory')).json();const box=$('mems');box.innerHTML='';
 d.memories.filter(m=>m.status=='active').reverse().forEach(m=>{
  const r=document.createElement('div');r.className='mem';
  r.innerHTML="<b></b><span></span><button>forget</button>";r.querySelector("b").textContent=m.kind;r.querySelector("span").textContent=m.content;
  r.querySelector('button').onclick=async()=>{await fetch('/api/forget?id='+m.id,{method:'POST'});loadMem()};box.appendChild(r)});
}
$('net').onchange=e=>{$('ssid').value=e.target.value;$('pass').focus()};
$('save').onclick=async()=>{
 const f=new URLSearchParams({ssid:$('ssid').value,pass:$('pass').value,key:$('key').value.trim(),model:$('model').value.trim(),
  name:$('name').value.trim(),cat:$('cat').value,tz:$('tz').value,idle:$('idle').value});
 say('saving...');const r=await fetch('/save',{method:'POST',body:f});say(await r.text());
};
$('impbtn').onclick=async()=>{
 const f=$('imp').files[0];if(!f)return say('pick a .json export first');
 const fd=new FormData();fd.append('file',f,'import.json');say('importing...');
 const r=await fetch('/api/import',{method:'POST',body:fd});say(await r.text());load();
};
$('clear').onclick=async()=>{if(confirm('Forget every reflection on the board?')){await fetch('/api/clear',{method:'POST'});load()}};
$('bodysave').onclick=async()=>{
 const r=await fetch('/api/body',{method:'POST',body:new URLSearchParams({text:$('body').value})});
 $('bodymsg').textContent=await r.text();
};
fetch('/api/body').then(r=>r.text()).then(t=>$('body').value=t).catch(()=>{});
load().catch(e=>say('could not load config: '+e));
</script></body></html>)HTML";
