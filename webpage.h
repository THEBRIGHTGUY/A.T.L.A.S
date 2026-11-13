// The local app. The gateway serves this page from its own WiFi, so it needs no internet.
#pragma once
const char INDEX_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Soil Monitor</title>
<style>
:root{--bg:#e8eee9;--card:#f8faf8;--ink:#1b2a22;--mute:#5d6f64;--line:#c5d1c8;--acc:#1f6f55;--bad:#a23b2a}
@media (prefers-color-scheme:dark){:root{--bg:#121a16;--card:#1b2620;--ink:#e3ece6;--mute:#8da095;--line:#2f3f36;--acc:#5fc4a0;--bad:#e88a78}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.45 ui-rounded,"Avenir Next",system-ui,sans-serif}
main{max-width:720px;margin:0 auto;padding:20px 16px 60px}
h1{font-size:1.6rem;margin:0 0 4px}
#st{color:var(--mute);margin:0 0 16px;font-size:.9rem}
#st.bad{color:var(--bad)}
.row{display:flex;gap:8px;margin-bottom:14px}
input{flex:1;min-width:0;padding:10px;border:1px solid var(--line);border-radius:8px;background:var(--card);color:inherit;font:inherit}
button{background:var(--acc);color:var(--bg);border:0;border-radius:8px;padding:10px 14px;font:inherit;cursor:pointer}
button.g{background:none;color:var(--mute);border:1px solid var(--line);padding:4px 8px}
button:focus-visible,input:focus-visible{outline:2px solid var(--acc);outline-offset:2px}
.sys{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px 16px;margin-bottom:14px}
.sys header{display:flex;justify-content:space-between;align-items:baseline;gap:8px}
.sys h2{font-size:1.1rem;margin:0}
.meta{color:var(--mute);font-size:.82rem}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(130px,1fr));gap:10px 14px;margin-top:12px}
.grid b{display:block;font-size:1.35rem;font-variant-numeric:tabular-nums}
.grid span{color:var(--mute);font-size:.8rem}
.ai{margin-top:12px;padding:10px 12px;border-left:3px solid var(--acc);background:var(--bg);border-radius:6px}
</style></head><body><main>
<h1>Soil Monitor</h1>
<p id="st">Connecting to gateway...</p>
<div class="row"><input id="code" placeholder="paste pairing link or code" autocapitalize="off"><button id="add">Add</button></div>
<div id="list"></div>
</main>
<script>
const $=s=>document.querySelector(s);
const load=(k,d)=>{try{return JSON.parse(localStorage.getItem(k))??d}catch{return d}};
const save=(k,v)=>{try{localStorage.setItem(k,JSON.stringify(v))}catch{}};
let systems=load('systems',[]),data={},linkOk=false;
const F=[['Soil moisture','moist','%'],['Soil temp','soilT','°C'],['pH','ph',''],['EC','ec','µS/cm'],
['Nitrogen','n','mg/kg'],['Phosphorus','p','mg/kg'],['Potassium','k','mg/kg'],['Light','lux','lx'],
['Air temp','airT','°C'],['Humidity','airH','%'],['Pressure','pres','hPa']];

function addSys(t){
  const m=/id=([0-9A-Fa-f]{6}).*?key=([0-9A-Fa-f]{8})/.exec(t)||/([0-9A-Fa-f]{6})([0-9A-Fa-f]{8})\s*$/.exec(t);
  if(!m)return false;
  const id=m[1].toUpperCase(),key=m[2].toUpperCase();
  if(!systems.some(s=>s.id===id)){systems.push({id,key});save('systems',systems);}
  return true;
}
const q=new URLSearchParams(location.search);
const pm=/^\/([0-9A-Fa-f]{6})([0-9A-Fa-f]{8})$/.exec(location.pathname);
if(pm){addSys(pm[1]+pm[2]);history.replaceState(null,'','/');}
if(q.get('id')&&q.get('key')){addSys('id='+q.get('id')+'&key='+q.get('key'));history.replaceState(null,'','/');}
$('#add').onclick=()=>{if(addSys($('#code').value)){$('#code').value='';poll();}};

async function poll(){
  let any=false;
  for(const s of systems){
    try{
      const r=await fetch('/api/node?id='+s.id+'&key='+s.key,{cache:'no-store'});
      data[s.id]=await r.json();any=true;
    }catch{}
  }
  linkOk=any||!systems.length;
  $('#st').textContent=linkOk?'Connected to gateway':'Cannot reach gateway. Join its WiFi network first.';
  $('#st').className=linkOk?'':'bad';
  render();
}
function el(t,c,x){const e=document.createElement(t);if(c)e.className=c;if(x!==undefined)e.textContent=x;return e}
function render(){
  const L=$('#list');L.replaceChildren();
  if(!systems.length){L.append(el('p','meta','No systems yet. Scan a node QR code with your phone camera app.'));return;}
  for(const s of systems){
    const d=data[s.id]||{};
    const c=el('section','sys'),h=el('header');
    const rm=el('button','g','Remove');
    rm.onclick=()=>{if(confirm('Remove System '+s.id+'?')){systems=systems.filter(x=>x.id!==s.id);save('systems',systems);render();}};
    h.append(el('h2','','System '+s.id),rm);c.append(h);
    let m='Waiting for first reading...';
    if(d.error)m='Wrong key for this system';
    else if(d.age!==undefined)m='Updated '+(d.age<90?d.age+' s':Math.round(d.age/60)+' min')+' ago, signal '+d.rssi+' dBm';
    c.append(el('div','meta',m));
    if(d.age!==undefined){
      const g=el('div','grid');
      for(const[l,k,u]of F){if(d[k]===undefined)continue;
        const b=el('div');b.append(el('b','',d[k]),el('span','',l+(u?' ('+u+')':'')));g.append(b);}
      c.append(g);
      if(d.ai)c.append(el('div','ai','AI: '+d.ai));
    }
    L.append(c);
  }
}
render();poll();setInterval(poll,3000);
</script></body></html>)HTML";
