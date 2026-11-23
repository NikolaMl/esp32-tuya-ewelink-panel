#pragma once
// Web aplikacija za podešavanje panela sa telefona (otvara se QR kodom sa panela: Podešavanja → Web).
// Sve u jednom fajlu; podaci idu preko /api/* (JSON), PIN sa ekrana u svakom zahtevu.
static const char WEB_APP_HTML[] = R"HTML(<!doctype html>
<html lang="sr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#0e1116">
<title>Panel</title>
<style>
:root{--bg:#0e1116;--card:#171c23;--card2:#1f262f;--line:#2b333d;--text:#e8ecf1;--muted:#8a95a3;--accent:#f6b93b;--ok:#3fb950;--err:#f85149}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html,body{margin:0;background:var(--bg);color:var(--text);font:16px/1.45 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif}
header{position:sticky;top:0;z-index:5;background:rgba(14,17,22,.92);backdrop-filter:blur(12px);-webkit-backdrop-filter:blur(12px);padding:14px 16px 10px;border-bottom:1px solid var(--line)}
h1{font-size:20px;margin:0 0 2px}
.sub{color:var(--muted);font-size:13px}
nav{display:flex;gap:6px;overflow-x:auto;margin-top:12px;scrollbar-width:none}
nav::-webkit-scrollbar{display:none}
nav button{flex:none;border:1px solid var(--line);background:var(--card);color:var(--text);padding:8px 14px;border-radius:999px;font:inherit;font-size:14px}
nav button.on{background:var(--accent);border-color:var(--accent);color:#231a00;font-weight:600}
main{max-width:1120px;margin:0 auto;padding:16px 16px 48px}
section{display:none}section.on{display:block}
.card{background:var(--card);border:1px solid var(--line);border-radius:16px;padding:16px;margin-bottom:14px}
.card h2{font-size:16px;margin:0 0 10px}
label{display:block;color:var(--muted);font-size:13px;margin:12px 0 5px}
input[type=text],input[type=password],input[type=number],select{width:100%;padding:11px 12px;border-radius:10px;border:1px solid var(--line);background:var(--bg);color:var(--text);font:inherit}
.btn{display:inline-flex;align-items:center;justify-content:center;gap:6px;border:1px solid var(--line);background:var(--card2);color:var(--text);padding:11px 18px;border-radius:12px;font:inherit;font-weight:600;text-decoration:none;cursor:pointer}
.btn.primary{background:var(--accent);border-color:var(--accent);color:#231a00}
.btn:disabled{opacity:.5}
.row{display:flex;gap:10px;flex-wrap:wrap;margin-top:14px}
.hint{color:var(--muted);font-size:13px;margin:8px 2px}
.stage{position:relative;width:100%;aspect-ratio:5/3;border-radius:14px;overflow:hidden;background:#101820;touch-action:none;user-select:none;-webkit-user-select:none}
.stage canvas{position:absolute;inset:0;width:100%;height:100%}
.mk{position:absolute;transform:translate(-50%,-50%);background:rgba(0,0,0,.58);border:1px solid rgba(255,255,255,.3);color:#fff;border-radius:12px;padding:4px 9px;text-align:center;font-weight:700;font-size:clamp(11px,3vw,17px);line-height:1.15;cursor:grab;touch-action:none;white-space:nowrap}
.mk small{display:block;font-weight:500;font-size:clamp(9px,2.2vw,12px);color:#d5dce4}
.mk.light{border-radius:999px;min-width:2.4em;min-height:2.4em;padding:6px 10px}
.mk.light.lit{background:rgba(246,185,59,.45);color:#fff;border-color:var(--accent);box-shadow:0 0 12px 2px rgba(246,185,59,.45)}
.mk.drag{outline:2px solid var(--accent);outline-offset:2px;z-index:3;cursor:grabbing}
.mk.off{opacity:.6}
.list .it{display:flex;align-items:center;gap:12px;padding:12px 0;border-top:1px solid var(--line)}
.list .it:first-of-type{border-top:0}
.dh{font-size:13px;text-transform:uppercase;letter-spacing:.06em;color:var(--muted);margin:18px 0 2px;font-weight:600}
.dh:first-child{margin-top:0}.dh span{font-weight:400;opacity:.7;margin-left:4px}
.dh+.it{border-top:0}
.it .t{flex:1;min-width:0}.it .n{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.it .d{color:var(--muted);font-size:13px}
.sw{position:relative;width:50px;height:30px;flex:none}
.sw input{opacity:0;width:0;height:0}
.sw span{position:absolute;inset:0;background:#39424d;border-radius:30px;transition:.2s}
.sw span:before{content:"";position:absolute;width:24px;height:24px;left:3px;top:3px;background:#fff;border-radius:50%;transition:.2s}
.sw input:checked+span{background:var(--accent)}
.sw input:checked+span:before{transform:translateX(20px)}
.src{display:inline-flex;align-items:center;justify-content:center;width:18px;height:18px;border-radius:5px;font-size:11px;font-weight:800;line-height:1;color:#fff;flex:none;vertical-align:-3px;margin-right:6px}
.src.t{background:#ff5a1f}.src.e{background:#2f7cf6}.src.p{background:var(--card2);color:var(--muted);border:1px solid var(--line)}
.pc .src{margin-right:0}
#status .src{width:15px;height:15px;font-size:10px;border-radius:4px;margin-right:4px;vertical-align:-2px}
.chip{display:inline-block;font-size:12px;padding:2px 8px;border-radius:999px;background:var(--card2);color:var(--muted);margin-left:6px}
.ok{color:var(--ok)}.err{color:var(--err)}
code{background:var(--bg);padding:2px 6px;border-radius:6px;word-break:break-all;font-size:13px}
input[type=range]{width:100%;accent-color:var(--accent)}
.prog{height:6px;background:var(--card2);border-radius:6px;overflow:hidden;margin-top:12px;display:none}
.prog i{display:block;height:100%;width:0;background:var(--accent);transition:width .2s}
#toast{position:fixed;left:50%;bottom:24px;transform:translateX(-50%) translateY(120px);background:#262d36;border:1px solid var(--line);padding:11px 18px;border-radius:12px;transition:transform .25s;z-index:9;max-width:90vw}
#toast.show{transform:translateX(-50%) translateY(0)}
#toast.bad{border-color:var(--err)}
#pinbox{max-width:360px;margin:12vh auto 0;text-align:center}
.lay{display:grid;gap:16px}
@media(min-width:900px){.lay{grid-template-columns:280px 1fr;align-items:start}.pc{width:100%}.pal{order:-1;position:sticky;top:120px;max-height:calc(100vh - 140px);overflow:auto}}
.cat h3{font-size:13px;text-transform:uppercase;letter-spacing:.06em;color:var(--muted);margin:4px 2px 8px}
.cat{margin-bottom:14px}
.pcs{display:flex;flex-wrap:wrap;gap:8px}
@media(min-width:900px){.pcs{flex-direction:column}}
.pc{display:flex;align-items:center;gap:8px;background:var(--card2);border:1px solid var(--line);border-radius:12px;padding:8px 12px;cursor:grab;touch-action:none;user-select:none;-webkit-user-select:none;font-size:14px}
.pc b{font-weight:600;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:150px;min-width:0;flex:0 1 auto}
.pc span,.pc em{flex:none}
.pc em{font-style:normal;color:var(--muted);margin-left:auto}
.pc.placed{border-color:var(--accent);background:rgba(246,185,59,.12)}
.pc.placed:after{content:"✓";color:var(--accent);font-weight:700}
.pc.multi:after{content:none}
.pc i{font-style:normal;font-size:12px;font-weight:700;color:#231a00;background:var(--accent);border-radius:999px;padding:1px 7px;flex:none}
.selh .acts{display:flex;gap:8px;flex:none}
.pc.off b{color:var(--muted)}
.stage.over{outline:3px dashed var(--accent);outline-offset:-3px}
.mk.rm{background:rgba(248,81,73,.9);border-color:#f85149}
.mk.alarm{background:#b3261e;border-color:#ff8a80}
.mk.act{border-color:#f0a020;box-shadow:0 0 0 1px #f0a020}
.mk.ghost{position:fixed;z-index:20;pointer-events:none;opacity:.92;transform:translate(-50%,-50%) scale(1.08)}
.mk.sel{outline:2px solid #fff;outline-offset:3px;z-index:2}
.mk.light.inv{width:8%;aspect-ratio:1;min-width:0;min-height:0;padding:0;display:flex;align-items:center;justify-content:center;background:rgba(255,255,255,.06);border:2px dashed rgba(255,255,255,.85);font-size:clamp(9px,2vw,14px)}
.mk.light.inv b{opacity:.55;font-weight:400}
.mk.light.inv small{position:absolute;top:100%;left:50%;transform:translateX(-50%);margin-top:4px;background:rgba(0,0,0,.6);padding:1px 6px;border-radius:6px}
.mk.light.inv.lit{background:rgba(246,185,59,.3);border-color:var(--accent);box-shadow:0 0 16px 6px rgba(246,185,59,.5)}
.mk.light.inv.lit small{color:#d5dce4}
.mk.light.inv.rm{background:rgba(248,81,73,.5)}
.scene{border:1px solid var(--line);border-radius:14px;padding:12px;margin-bottom:12px;background:var(--card2)}
.sch{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-bottom:8px}
.sch input{flex:1 1 180px;min-width:0;font-weight:600}
.scrow{display:grid;grid-template-columns:minmax(0,1.6fr) minmax(0,1fr) auto;gap:6px;margin-top:6px}
.scrow select{padding:8px 8px}
.pgbar{display:flex;gap:6px;overflow-x:auto;margin-bottom:10px;scrollbar-width:none}
.pgbar::-webkit-scrollbar{display:none}
.pgbar button{flex:none;border:1px solid var(--line);background:var(--card2);color:var(--text);padding:7px 14px;border-radius:999px;font:inherit;font-size:14px;cursor:pointer}
.pgbar button.on{background:var(--accent);border-color:var(--accent);color:#231a00;font-weight:600}
.pgbar button.add{color:var(--muted);border-style:dashed}
.pged{display:flex;gap:8px;margin-top:10px;align-items:center;flex-wrap:wrap}
.pged input{flex:1 1 160px;min-width:0;padding:8px 10px}
.pged .acts{display:flex;gap:8px;flex-wrap:wrap}
.selc{display:none;margin-top:12px;padding:12px;border:1px solid var(--line);border-radius:12px;background:var(--card2)}
.selh{display:flex;align-items:center;gap:10px;justify-content:space-between;flex-wrap:wrap;margin-bottom:10px}
.selh b{min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.btn.sm{padding:7px 12px;font-size:14px;border-radius:10px}
.seg{display:flex;background:var(--bg);border:1px solid var(--line);border-radius:12px;padding:3px;gap:3px}
.seg button{flex:1;border:0;background:transparent;color:var(--text);padding:9px 10px;border-radius:9px;font:inherit;font-size:14px;font-weight:600;cursor:pointer}
.seg button.on{background:var(--accent);color:#231a00}
#pinbox input{text-align:center;font-size:28px;letter-spacing:6px}
</style></head><body>
<div id="pinbox" style="display:none"><div class="card">
<h1>Podešavanje panela</h1><p class="sub">Unesi PIN prikazan na panelu<br>(Podešavanja → Web)</p>
<input id="pinIn" type="text" inputmode="numeric" maxlength="6" autocomplete="off">
<div class="row" style="justify-content:center"><button class="btn primary" id="pinGo">Nastavi</button></div>
<p class="err" id="pinErr"></p></div></div>

<div id="app" style="display:none">
<header><h1>Panel</h1><div class="sub" id="status"></div>
<nav id="tabs"><button data-t="layout" class="on">Raspored</button><button data-t="scenes">Scene</button><button data-t="devices">Uređaji</button><button data-t="screen">Ekran</button><button data-t="acc">Nalozi</button></nav></header>
<main>
<section id="layout" class="on"><div class="lay">
<div class="card" style="margin:0"><div class="pgbar" id="pgTabs"></div>
<div class="stage" id="stage"><canvas id="bgc" width="800" height="480"></canvas><canvas id="cropc" width="800" height="480" style="display:none"></canvas></div>
<div class="pged" id="pgEd"><input type="text" id="pgName" maxlength="32" placeholder="Naziv dela (npr. Ulaz)">
<span class="acts"><button class="btn sm" id="pick">📷 Fotografija</button><button class="btn sm" id="bgReset">Vrati ugrađenu</button><button class="btn sm" id="pgDel">Obriši deo</button></span></div>
<input type="file" id="file" accept="image/*" style="display:none">
<div class="selc" id="crop"><div class="selh"><b id="cropTitle">Nova fotografija</b></div>
<p class="hint" style="margin-top:0">Prevuci sliku prstom ili pomeri klizače; oznake uređaja ostaju na mestu, pa ih poravnaj sa slikom.</p>
<label>Levo / desno</label><input type="range" id="offX" min="0" max="100" value="50">
<label>Gore / dole</label><input type="range" id="offY" min="0" max="100" value="50">
<label>Uvećanje</label><input type="range" id="zoom" min="100" max="250" value="100">
<div class="row"><button class="btn primary" id="send">Pošalji na panel</button><button class="btn" id="cropCancel">Otkaži</button></div>
<div class="prog" id="prog"><i></i></div></div>
<div class="selc" id="sel"></div>
<p class="hint">Prevuci uređaj sa liste na sliku da se pojavi na početnom ekranu. Oznaku pomeraš prevlačenjem,
a skloniš je kad je prevučeš van slike. Dodir na oznaku svetla ili roletne: izbor izgleda (dugme ili nevidljivo polje preko
sijalice ili prozora na slici). Na panelu se vidi posle „Sačuvaj“. Više delova kuće: „+ Deo“ gore, svakom delu
fotografija dugmetom „📷 Fotografija“; na panelu se delovi menjaju prevlačenjem levo/desno.</p>
<div class="row"><button class="btn primary" id="saveL" disabled>Sačuvaj raspored</button><button class="btn" id="resetL">Podrazumevani raspored</button></div></div>
<aside class="card pal" id="pal" style="margin:0"></aside>
</div></section>

<section id="scenes"><div class="card"><h2>Scene</h2>
<p class="hint">Scena jednim dodirom podesi više uređaja (npr. „Odlazim“: ugasi svetla, spusti roletne, uključi alarm).
Na panelu je u listi uređaja (sekcija Scene), a dugme možeš staviti i na sliku: tab Raspored → Scene. Šalju se samo
komande koje nešto menjaju; za isključivanje uključenog alarma panel traži PIN.</p>
<div id="scList"></div>
<div class="row"><button class="btn" id="scAdd">+ Nova scena</button><button class="btn primary" id="scSave" disabled>Sačuvaj scene</button></div></div></section>

<section id="devices"><div class="card"><h2>Uređaji na panelu</h2>
<p class="hint">Isključen uređaj panel potpuno ignoriše: nema ga u listi ni na slici, ne šalje mu nikakve komande (ni iz scena),
ne prikazuje njegova upozorenja (ni za dim ni za alarm) i ne traži podatke o njemu. Uključi ga ponovo ovde ili na panelu
(Podešavanja → Uređaji); oznake na slici i scene ostaju sačuvane.</p><div class="list" id="devList"></div></div></section>

<section id="screen"><div class="card"><h2>Ekran</h2>
<label>Zatamni posle (sekundi bez dodira)</label><input type="number" id="dimSec" min="5" max="3600">
<label>Ugasi posle (sekundi; 0 = nikad)</label><input type="number" id="offSec" min="0" max="36000">
<div class="row"><button class="btn primary" id="saveS">Sačuvaj</button></div>
<p class="hint">Dodir na ugašen ekran ga samo pali. Za vreme alarma ekran se ne gasi.</p></div></section>
<section id="acc">
<div class="card"><h2>Tuya / Smart Life <span class="chip" id="tuyaSt"></span></h2>
<label>Data centar</label><select id="tuyaEp"></select>
<label>Access ID / Client ID</label><input type="text" id="tuyaId" autocomplete="off">
<label>Access Secret / Client Secret</label><input type="password" id="tuyaSecret" autocomplete="off">
<div class="row"><button class="btn primary" id="saveT">Sačuvaj</button></div></div>
<div class="card"><h2>eWeLink <span class="chip" id="ewSt"></span></h2>
<label>App ID</label><input type="text" id="ewId" autocomplete="off">
<label>App Secret</label><input type="password" id="ewSecret" autocomplete="off">
<div class="row"><button class="btn primary" id="saveE">Sačuvaj</button><a class="btn" id="ewLink">Poveži nalog</a></div>
<p class="hint">Redirect URL za dev.ewelink.cc: <code id="redir"></code></p></div>
<div class="card"><h2>Wi-Fi</h2>
<label>Naziv mreže (SSID)</label><input type="text" id="ssid" autocomplete="off">
<label>Lozinka</label><input type="password" id="wpass" autocomplete="off">
<div class="row"><button class="btn primary" id="saveW">Poveži</button></div>
<p class="hint">Pažnja: ako je lozinka pogrešna, panel gubi vezu i Wi-Fi se ponovo podešava na samom panelu.</p></div>
</section>
</main></div>
<div id="toast"></div>

<script>
const $=s=>document.querySelector(s);
let pin=new URLSearchParams(location.search).get('pin')||'',S=null,dirty=false,ON=false;
function toast(m,bad){if(!m)return;const t=$('#toast');t.textContent=m;t.className='show'+(bad?' bad':'');clearTimeout(t._h);t._h=setTimeout(()=>t.className='',2800)}
// 403/429: PIN ne važi (panel restartovan, nov PIN) ili previše pokušaja -> polje za PIN; izmene ostaju
async function api(p,o={}){const r=await fetch(p+(p.includes('?')?'&':'?')+'pin='+encodeURIComponent(pin),o);
 if(r.status==403||r.status==429){const m=r.status==429?await r.text():ON?'PIN se promenio jer je panel ponovo pokrenut. Unesi novi PIN sa panela (Podešavanja → Web) ili ponovo skeniraj QR kod.':'Pogrešan PIN (menja se pri svakom pokretanju panela)';
  ON=false;showPin(m);throw new Error('')}
 if(!r.ok)throw new Error((await r.text())||('HTTP '+r.status));return r}
const post=(p,b)=>api(p,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)}).then(r=>r.json());
const esc=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const short=n=>n.replace(/ THS$/,'');

async function start(){
 if(!pin){showPin();return}
 try{if(S&&dirty){const n=await (await api('/api/state')).json();S.tuya=n.tuya;renderStatus()}else await load();  // nesačuvan raspored ostaje
  ON=true;$('#pinbox').style.display='none';$('#app').style.display='block';history.replaceState(null,'','/?pin='+pin+location.hash)}
 catch(e){toast(e.message,1)}}
function showPin(err){$('#app').style.display='none';$('#pinbox').style.display='block';$('#pinErr').textContent=err||'';$('#pinIn').value='';$('#pinIn').focus()}
$('#pinGo').onclick=()=>{pin=$('#pinIn').value.trim();start()};
$('#pinIn').onkeydown=e=>{if(e.key=='Enter')$('#pinGo').click()};

async function load(){S=await (await api('/api/state')).json();renderStatus();renderLayout();renderScenes();renderDevices();renderScreen();renderAcc()}
function stText(c){return{ok:'povezano',connecting:'povezivanje…',disabled:c.msg||'nije podešen',error:c.msg||'greška'}[c.state]}
function renderStatus(){$('#status').innerHTML=esc(S.wifi)+' · '+esc(S.ip)+' · <span class="src t">T</span>Tuya: '+esc(stText(S.tuya))
 +(S.ewelink&&S.ewelink.state!='disabled'?' · <span class="src e">e</span>eWeLink: '+esc(stText(S.ewelink)):'')}

// ---------- raspored: L = oznake na slici [{k,x,y,s}] (centar u pikselima panela 800x480).
// Prekidač može na sliku više puta; temperatura najviše jednom. SEL = izabrana oznaka (objekat iz L).
let L=[],D=null,SEL=null,PG=0,bgCache={};  // PG = prikazani deo kuće
const placeable=it=>'slcdxtvm'.includes(it.t);
const once=t=>'sdtvm'.includes(t);  // temperatura, dim, termostat, vrata, pokret: najviše jednom po delu
const round=t=>t=='l'||t=='c';  // okrugla oznaka: dugme ili nevidljivo polje
const itemOf=k=>S.items.find(i=>i.k==k);
const onPg=p=>p.g==PG;
const countOf=k=>L.filter(p=>p.k==k&&onPg(p)).length;
// Grupe uređaja kao u listi na panelu: [ključ, naziv, ikonica, da li uređaj pripada grupi]
const sw=g=>i=>i.t=='l'&&(i.grp||'switch')==g;
const CATS=[['a','Alarm','🔔',i=>i.t=='a'],['x','Scene','🎬',i=>i.t=='x'],['s','Temperatura','🌡️',i=>i.t=='s'],['light','Svetla','💡',sw('light')],['switch','Prekidači','🔘',sw('switch')],
 ['socket','Utičnice','🔌',sw('socket')],['c','Roletne','🪟',i=>i.t=='c'],['t','Termostati','♨️',i=>i.t=='t'],['climate','Klima','❄️',sw('climate')],
 ['d','Detektori dima','🔥',i=>i.t=='d'],['v','Vrata i prozori','🚪',i=>i.t=='v'],['m','Pokret','🏃',i=>i.t=='m'],
 ['security','Sigurnost','🛡️',sw('security')],['other','Ostalo','⚙️',sw('other')]];
const catOf=it=>CATS.find(c=>c[3](it))||CATS[3];
const first=v=>esc(String(v||'').split(' · ')[0]);
const mkHtml=it=>it.t=='x'?'▶ '+esc(it.n):(it.t=='s'?esc(it.v):it.t=='d'?(it.al?'⚠️ DIM!':'🔥 Dim'):it.t=='c'?'<b>⇅</b>'
 :it.t=='t'?'♨️ '+first(it.v):it.t=='v'?'🚪 '+first(it.v):it.t=='m'?'🏃 '+first(it.v):'<b>'+catOf(it)[2]+'</b>')+'<small>'+esc(short(it.n))+'</small>';
function setDirty(v){dirty=v;$('#saveL').disabled=!v}
function changed(){renderMarkers();renderPalette();renderSel();setDirty(true)}
// Nepoznati uređaji (npr. oblak još nije učitan) ostaju u L da se ne izgube pri čuvanju, samo se ne crtaju
function renderLayout(){const i=L.indexOf(SEL),k=SEL&&SEL.k;L=(S.layout||[]).map(p=>({k:p.k,x:p.x,y:p.y,s:p.s||0,g:p.g||0}));
 if(PG>=S.pages.length)PG=S.pages.length-1;SEL=i>=0&&L[i]&&L[i].k==k?L[i]:null;renderPages();drawBg();renderMarkers();renderPalette();renderSel();setDirty(false)}
const pgName=i=>S.pages[i].n||('Deo '+(i+1));
function renderPages(){const n=S.pages.length;
 $('#pgTabs').innerHTML=S.pages.map((p,i)=>`<button data-i="${i}" class="${i==PG?'on':''}">${esc(pgName(i))}${p.img?'':' · bez slike'}</button>`).join('')
  +(n<6?'<button class="add" id="pgAdd">+ Deo</button>':'');
 $('#pgTabs').querySelectorAll('button[data-i]').forEach(b=>b.onclick=()=>{cancelCrop();PG=+b.dataset.i;SEL=null;renderPages();drawBg();renderMarkers();renderPalette();renderSel()});
 if($('#pgAdd'))$('#pgAdd').onclick=addPage;
 $('#pgName').value=S.pages[PG].n;$('#pgDel').style.display=n>1?'':'none';renderBgBtn()}
async function addPage(){const name=prompt('Naziv novog dela (npr. Ulaz, Terasa):','');if(name===null)return;
 try{await post('/api/pages',{op:'add',name});cancelCrop();S.pages.push({n:name.trim(),img:false,own:false});PG=S.pages.length-1;SEL=null;
  renderPages();drawBg();renderMarkers();renderPalette();renderSel();toast('Dodat deo — dodaj mu fotografiju dugmetom „📷 Fotografija“')}catch(e){toast(e.message,1)}}
$('#pgName').onchange=async()=>{S.pages[PG].n=$('#pgName').value.trim();try{await post('/api/pages',{op:'names',names:S.pages.map(p=>p.n)});renderPages();toast('Naziv sačuvan')}catch(e){toast(e.message,1)}};
$('#pgDel').onclick=async()=>{if(dirty)return toast('Prvo sačuvaj raspored',1);
 if(!confirm('Obrisati deo „'+pgName(PG)+'“ sa njegovom fotografijom i svim oznakama?'))return;
 try{await post('/api/pages',{op:'del',i:PG});cancelCrop();bgCache={};PG=Math.max(0,PG-1);S=await (await api('/api/state')).json();renderLayout();toast('Deo je obrisan')}catch(e){toast(e.message,1)}};
// Fotografija prikazanog dela (keš po delu; null = deo nema sliku)
async function drawBg(){const pg=PG,c=$('#bgc'),g=c.getContext('2d');
 if(bgCache[pg]===undefined){g.fillStyle='#101820';g.fillRect(0,0,800,480);
  try{const r=S.pages[pg].img?await fetch('/api/bg?page='+pg+'&pin='+encodeURIComponent(pin)):{status:404};if(r.status==404)bgCache[pg]=null;else if(!r.ok)throw new Error('HTTP '+r.status);else{const px=new Uint16Array(await r.arrayBuffer()),im=g.createImageData(800,480),d=im.data;
   for(let i=0;i<px.length&&i<384000;i++){const v=px[i],R=v>>11,G=(v>>5)&63,B=v&31;d[i*4]=R<<3|R>>2;d[i*4+1]=G<<2|G>>4;d[i*4+2]=B<<3|B>>2;d[i*4+3]=255}
   bgCache[pg]=im}}catch(e){return toast('Slika: '+e.message,1)}}
 if(pg!=PG)return;
 if(bgCache[pg])g.putImageData(bgCache[pg],0,0);
 else{g.fillStyle='#101820';g.fillRect(0,0,800,480);g.fillStyle='#8a95a3';g.font='28px sans-serif';g.textAlign='center';
  g.fillText('Ovaj deo nema fotografiju',400,225);g.fillText('Dodaj je dugmetom „📷 Fotografija“ ispod',400,265)}}
function place(d,x,y){d.style.left=(x/8)+'%';d.style.top=(y/4.8)+'%'}
// Isključen uređaj (it.h): oznaka ostaje sačuvana u L, ali se ne prikazuje
function renderMarkers(){const st=$('#stage');st.querySelectorAll('.mk').forEach(e=>e.remove());
 L.forEach(p=>{const it=itemOf(p.k);if(!it||it.h||!placeable(it)||!onPg(p))return;const d=document.createElement('div');
  d.className='mk'+(round(it.t)?' light'+(p.s==1?' inv':'')+(it.t=='l'&&it.on?' lit':''):'')+(it.o?'':' off')+(it.al?(it.t=='d'?' alarm':' act'):'')+(p==SEL?' sel':'');d._p=p;place(d,p.x,p.y);d.innerHTML=mkHtml(it);
  d.onpointerdown=e=>dragStart(e,p.k,p,d,null);st.appendChild(d)})}
// Odakle uređaj dolazi: ikonica (slovo u boji) + pun naziv u title / opisu
const SRC={t:['T','Tuya'],e:['e','eWeLink'],p:['P','panel']};
function srcOf(i){return SRC[i.c]||SRC.t}
function srcIc(i){const s=srcOf(i);return `<span class="src ${SRC[i.c]?i.c:'t'}" title="${s[1]}">${s[0]}</span>`}
function renderPalette(){
 $('#pal').innerHTML=CATS.filter(c=>c[0]!='a').map(([t,title,ic,has])=>{const its=S.items.filter(i=>has(i)&&!i.h);if(!its.length)return'';
  return `<div class="cat"><h3>${title}</h3><div class="pcs">`+its.map(i=>{const n=countOf(i.k);
   return `<div class="pc${n?' placed':''}${n>1?' multi':''}${i.o?'':' off'}" data-k="${esc(i.k)}" title="${esc(i.n)} · ${srcOf(i)[1]}"><span>${ic}</span>${srcIc(i)}<b>${esc(short(i.n))}</b>${i.v?`<em>${first(i.v)}</em>`:''}${n>1?`<i>${n}×</i>`:''}</div>`}).join('')+'</div></div>'}).join('')
  +'<p class="hint">Prevuci na sliku ili dodirni za dodavanje. Prekidač i roletna mogu na sliku više puta (npr. na svaku sijalicu). ✓ = već je na ovom delu. Izvor: <span class="src t">T</span>Tuya <span class="src e">e</span>eWeLink <span class="src p">P</span>napravljeno na panelu.</p>';
 $('#pal').querySelectorAll('.pc').forEach(c=>c.onpointerdown=e=>dragStart(e,c.dataset.k,null,null,c))}
function renderSel(){const b=$('#sel'),p=SEL,it=p&&itemOf(p.k);if(!it||it.h){b.style.display='none';return}
 b.style.display='block';const inv=p.s==1,n=countOf(p.k),nr=L.filter(q=>q.k==p.k&&onPg(q)).indexOf(p)+1;
 b.innerHTML='<div class="selh"><b>'+srcIc(it)+esc(it.n)+(n>1?' <span class="chip">'+nr+' od '+n+'</span>':'')+'</b><span class="acts">'
  +(round(it.t)?'<button class="btn sm" id="selDup">+ Još jedno</button>':'')+'<button class="btn sm" id="selRm">Ukloni</button></span></div>'+(round(it.t)?
  '<div class="seg" id="selSt"><button data-s="0" class="'+(inv?'':'on')+'">● Dugme</button><button data-s="1" class="'+(inv?'on':'')+'">◌ Nevidljivo</button></div>'+
  '<p class="hint">'+(it.t=='c'?(inv?'Na panelu nema dugmeta, samo jedva vidljiv krug. Postavi isprekidani krug preko prozora sa roletnom: dodir na to mesto otvara Gore / Stop / Dole.'
  :'Okruglo dugme sa strelicama; dodir otvara Gore / Stop / Dole.')
  :(inv?'Na panelu nema dugmeta, samo jedva vidljiv krug. Postavi isprekidani krug preko sijalice na slici: dodir na to mesto pali/gasi svetlo, a upaljeno zasija.'
  :'Okruglo dugme sa ikonicom; kad je svetlo upaljeno, dugme je providno žuto.')+(n>1?' Sve oznake ovog prekidača pale isto svetlo.':''))+'</p>':'<p class="hint">'+(it.t=='x'?'Dugme scene: dodir na panelu pokreće scenu (uređaji se podešavaju u tabu Scene).':it.t=='d'?'Detektor dima: na panelu kvačica kad je sve u redu, crveno „DIM!“ kad javi dim (uz crveno upozorenje preko celog ekrana).'
  :it.t=='t'?'Termostat: na panelu trenutna i željena temperatura; dodir otvara − / + i uključi/isključi.'
  :it.t=='v'?'Senzor vrata/prozora: na panelu „Otvoreno“ (narandžasto) ili „Zatvoreno“.'
  :it.t=='m'?'Senzor pokreta: na panelu „Pokret“ (žuto) ili „Mirno“. Panel stanje proverava na 10 s do 5 min, pa kratak pokret može da propusti.'
  :'Temperatura se prikazuje bez naziva.')+'</p>');
 $('#selRm').onclick=()=>{L.splice(L.indexOf(p),1);SEL=null;changed()};
 if(round(it.t))$('#selDup').onclick=()=>{const q={k:p.k,x:p.x+40>780?p.x-40:p.x+40,y:p.y,s:p.s,g:p.g};L.push(q);SEL=q;changed()};
 b.querySelectorAll('#selSt button').forEach(x=>x.onclick=()=>{p.s=+x.dataset.s;changed()})}
function select(p){SEL=p;renderMarkers();renderSel()}
function flash(p){const m=[...document.querySelectorAll('#stage .mk')].find(x=>x._p==p);if(m){m.classList.add('drag');setTimeout(()=>m.classList.remove('drag'),800)}}
$('#stage').addEventListener('pointerdown',e=>{if(!e.target.closest('.mk')&&SEL)select(null)});
function stagePt(e){const r=$('#stage').getBoundingClientRect();
 return{inside:e.clientX>=r.left&&e.clientX<=r.right&&e.clientY>=r.top&&e.clientY<=r.bottom,
  x:Math.round(Math.max(0,Math.min(800,(e.clientX-r.left)/r.width*800))),y:Math.round(Math.max(0,Math.min(480,(e.clientY-r.top)/r.height*480)))}}
function dragStart(e,k,p,marker,chip){e.preventDefault();try{(marker||chip).setPointerCapture(e.pointerId)}catch(_){}
 D={k,p,marker,chip,sx:e.clientX,sy:e.clientY,moved:false,ghost:null};if(marker)marker.classList.add('drag')}
function dragCleanup(d){$('#stage').classList.remove('over');if(d.ghost)d.ghost.remove();if(d.marker)d.marker.classList.remove('drag','rm')}
document.addEventListener('pointermove',e=>{if(!D)return;
 if(!D.moved&&Math.hypot(e.clientX-D.sx,e.clientY-D.sy)<5)return;D.moved=true;const p=stagePt(e);
 if(D.marker){place(D.marker,p.x,p.y);D.marker.classList.toggle('rm',!p.inside)}
 else{if(!D.ghost){D.ghost=document.createElement('div');const it=itemOf(D.k);D.ghost.className='mk ghost'+(round(it.t)?' light':'');D.ghost.innerHTML=mkHtml(it);document.body.appendChild(D.ghost)}
  D.ghost.style.left=e.clientX+'px';D.ghost.style.top=e.clientY+'px';$('#stage').classList.toggle('over',p.inside)}});
document.addEventListener('pointerup',e=>{if(!D)return;const d=D,pt=stagePt(e);D=null;dragCleanup(d);
 if(d.marker){if(!d.moved)return select(SEL==d.p?null:d.p);
  if(pt.inside){d.p.x=pt.x;d.p.y=pt.y}else{L.splice(L.indexOf(d.p),1);if(SEL==d.p)SEL=null;toast('Sklonjeno sa slike — „Sačuvaj“ da važi i na panelu')}
  return changed()}
 // sa liste: prekidač se uvek dodaje kao nova oznaka; temperatura postoji najviše jednom
 const old=once(itemOf(d.k).t)?L.find(q=>q.k==d.k&&onPg(q)):null;
 if(d.moved){if(!pt.inside)return;if(old){old.x=pt.x;old.y=pt.y;SEL=old}else{SEL={k:d.k,x:pt.x,y:pt.y,s:0,g:PG};L.push(SEL)}}
 else if(old){select(old);flash(old);return}
 else{const n=countOf(d.k)%5;SEL={k:d.k,x:400+n*30,y:240+n*30,s:0,g:PG};L.push(SEL)}
 changed()});
document.addEventListener('pointercancel',()=>{if(D){dragCleanup(D);D=null;renderMarkers()}});
$('#saveL').onclick=async()=>{const p=L.map(v=>({k:v.k,x:v.x,y:v.y,s:v.s||0,g:v.g||0}));
 try{await post('/api/layout',{p});toast('Raspored sačuvan, panel je osvežen');S=await (await api('/api/state')).json();renderLayout()}catch(e){toast(e.message,1)}};
$('#resetL').onclick=async()=>{if(!confirm('Vratiti podrazumevani raspored?'))return;
 try{await post('/api/layout',{reset:true});S=await (await api('/api/state')).json();renderLayout();toast('Vraćen podrazumevani raspored')}catch(e){toast(e.message,1)}};

// ---------- scene: SC = kopija za uređivanje, čuva se dugmetom „Sačuvaj scene“
let SC=[],scDirty=false;
const ACT_OPS={l:[['on','Upali'],['off','Ugasi']],c:[['open','Podigni'],['close','Spusti'],['stop','Zaustavi']],
 a:[['away','Uključi – odsutan'],['home','Uključi – kod kuće'],['disarm','Isključi (PIN na panelu)']]};
// Termostat: isključi/uključi (ako ima prekidač) i „Uključi na X°“ u njegovom opsegu (5–30°)
const tFmt=v=>String(+v.toFixed(1)).replace('.',',')+'°';
const tempOps=it=>{const r=[],st=Math.max(it.st||0.5,0.5),mn=Math.max(it.mn||5,5),mx=Math.min(it.mx||30,30);
 for(let v=Math.ceil(mn/st-1e-9)*st;v<=mx+1e-9;v+=st)r.push(['t'+(+v.toFixed(1)),'Uključi na '+tFmt(v)]);return r};
const opsOf=it=>it.t=='t'?[...(it.pw?[['off','Isključi'],['on','Uključi']]:[]),...tempOps(it)]:ACT_OPS[it.t]||[];
const actionable=it=>opsOf(it).length>0;
const defOp=it=>it.t=='l'?'off':it.t=='t'&&!it.pw?'t'+(tempOps(it).some(o=>o[0]=='t18')?18:+tempOps(it)[0][0].slice(1)):opsOf(it)[0][0];
function setScDirty(v){scDirty=v;$('#scSave').disabled=!v}
function renderScenes(force){if(scDirty&&!force)return;SC=JSON.parse(JSON.stringify(S.scenes||[]));setScDirty(false);drawScenes()}
function devOptions(sel){return CATS.map(([t,title,ic,has])=>{const its=S.items.filter(i=>has(i)&&!i.h&&actionable(i));if(!its.length)return'';
 return `<optgroup label="${ic} ${title}">`+its.map(i=>`<option value="${esc(i.k)}"${i.k==sel?' selected':''}>${esc(i.n)} · ${srcOf(i)[1]}</option>`).join('')+'</optgroup>'}).join('')}
function drawScenes(){
 $('#scList').innerHTML=SC.length?SC.map((s,i)=>`<div class="scene" data-i="${i}"><div class="sch"><input type="text" class="scName" maxlength="32" value="${esc(s.n)}" placeholder="Naziv scene">`
  +`<button class="btn sm" data-run>▶ Pokreni</button><button class="btn sm" data-del>Obriši</button></div>`
  +(s.a.length?'':'<p class="hint" style="margin:0">Dodaj uređaje koje scena podešava.</p>')
  +s.a.map((a,j)=>{const it=itemOf(a.k),ops=it?opsOf(it):[];
   return `<div class="scrow" data-j="${j}"><select class="scDev">${!it?`<option value="${esc(a.k)}" selected>(uređaj više ne postoji)</option>`:it.h?`<option value="${esc(a.k)}" selected>${esc(it.n)} (isključen, preskače se)</option>`:''}${devOptions(a.k)}</select>`
    +`<select class="scOp">${ops.map(([v,l])=>`<option value="${v}"${v==a.o?' selected':''}>${l}</option>`).join('')}</select><button class="btn sm" data-rm title="Ukloni">✕</button></div>`}).join('')
  +`<div class="row" style="margin-top:10px"><button class="btn sm" data-add>+ Uređaj</button><button class="btn sm" data-alloff>+ Ugasi sva svetla</button></div></div>`).join('')
  :'<p class="hint">Još nema scena. „+ Nova scena“ ispod.</p>';
 $('#scList').querySelectorAll('.scene').forEach(el=>{const s=SC[+el.dataset.i];
  el.querySelector('.scName').oninput=e=>{s.n=e.target.value;setScDirty(true)};
  el.querySelector('[data-del]').onclick=()=>{if(!confirm('Obrisati scenu „'+(s.n||'bez naziva')+'“?'))return;SC.splice(SC.indexOf(s),1);setScDirty(true);drawScenes()};
  el.querySelector('[data-run]').onclick=async()=>{if(scDirty||!s.id)return toast('Prvo sačuvaj scene',1);
   try{await post('/api/scenes/run',{id:s.id});toast('Scena je pokrenuta na panelu')}catch(e){toast(e.message,1)}};
  el.querySelector('[data-add]').onclick=()=>{const it=S.items.find(i=>!i.h&&actionable(i)&&!s.a.some(a=>a.k==i.k))||S.items.find(i=>!i.h&&actionable(i));
   if(!it)return toast('Nema uređaja za scenu',1);s.a.push({k:it.k,o:defOp(it)});setScDirty(true);drawScenes()};
  el.querySelector('[data-alloff]').onclick=()=>{let n=0;S.items.filter(i=>i.t=='l'&&!i.h&&['light','switch'].includes(i.grp||'switch')).forEach(i=>{
   const a=s.a.find(x=>x.k==i.k);if(a){if(a.o!='off'){a.o='off';n++}}else{s.a.push({k:i.k,o:'off'});n++}});setScDirty(true);drawScenes();
   toast(n?'Dodato svetala: '+n:'Sva svetla su već u sceni')};
  el.querySelectorAll('.scrow').forEach(r=>{const a=s.a[+r.dataset.j];
   r.querySelector('.scDev').onchange=e=>{const it=itemOf(e.target.value);a.k=e.target.value;a.o=defOp(it);setScDirty(true);drawScenes()};
   r.querySelector('.scOp').onchange=e=>{a.o=e.target.value;setScDirty(true)};
   r.querySelector('[data-rm]').onclick=()=>{s.a.splice(s.a.indexOf(a),1);setScDirty(true);drawScenes()}})})}
$('#scAdd').onclick=()=>{SC.push({n:'Nova scena',a:[]});setScDirty(true);drawScenes();const inp=[...document.querySelectorAll('.scName')].pop();if(inp){inp.focus();inp.select()}};
$('#scSave').onclick=async()=>{if(SC.some(s=>!s.n.trim()))return toast('Svaka scena treba naziv',1);
 try{await post('/api/scenes',{scenes:SC.map(s=>({id:s.id,n:s.n.trim(),a:s.a}))});const n=await (await api('/api/state')).json();
  S.items=n.items;S.scenes=n.scenes;if(!dirty){S.layout=n.layout;renderLayout()}else{renderPalette();renderMarkers()}renderScenes(true);renderDevices();toast('Scene sačuvane')}catch(e){toast(e.message,1)}};

// ---------- uređaji
function renderDevices(){
 $('#devList').innerHTML=CATS.map(([t,title,ic,has])=>{const its=S.items.filter(has);if(!its.length)return'';
  return `<h3 class="dh">${ic} ${title} <span>${its.length}</span></h3>`+its.map(it=>`<div class="it"><div class="t"><div class="n">${srcIc(it)}${esc(it.n)}</div><div class="d">${srcOf(it)[1]} · ${it.v?esc(it.v)+' · ':''}${it.o?'dostupan':'nedostupan'}</div></div><label class="sw"><input type="checkbox" data-k="${esc(it.k)}" ${it.h?'':'checked'}><span></span></label></div>`).join('')}).join('');
 $('#devList').querySelectorAll('input').forEach(i=>i.onchange=async()=>{try{await post('/api/hidden',{k:i.dataset.k,h:!i.checked});
  S.items.find(x=>x.k==i.dataset.k).h=!i.checked;toast(i.checked?'Uključen':'Isključen: panel ga potpuno ignoriše');renderPalette();renderMarkers();renderSel();drawScenes()}catch(e){i.checked=!i.checked;toast(e.message,1)}})}

// ---------- ekran
function renderScreen(){$('#dimSec').value=S.screen.dimSec;$('#offSec').value=S.screen.offSec}
$('#saveS').onclick=async()=>{const b={dimSec:+$('#dimSec').value,offSec:+$('#offSec').value};
 try{await post('/api/screen',b);S.screen=b;renderLayout();toast('Sačuvano')}catch(e){toast(e.message,1)}};

// ---------- fotografija prikazanog dela: iseci preko same slike (oznake ostaju vidljive), pa pošalji
let img=null,cropPg=-1,pan=null;
$('#pick').onclick=()=>$('#file').click();
$('#file').onchange=()=>{const f=$('#file').files[0];$('#file').value='';if(!f)return;const i=new Image();
 i.onload=()=>{img=i;cropPg=PG;$('#offX').value=$('#offY').value=50;$('#zoom').value=100;$('#cropTitle').textContent='Nova fotografija za „'+pgName(PG)+'“';
  $('#crop').style.display='block';$('#cropc').style.display='block';drawCrop();$('#crop').scrollIntoView({behavior:'smooth',block:'nearest'})};
 i.onerror=()=>toast('Ne mogu da otvorim sliku',1);i.src=URL.createObjectURL(f)};
function cancelCrop(){img=null;cropPg=-1;pan=null;$('#crop').style.display='none';$('#cropc').style.display='none';$('#prog').style.display='none'}
$('#cropCancel').onclick=cancelCrop;
$('#offX').oninput=$('#offY').oninput=$('#zoom').oninput=()=>drawCrop();
function cropSize(){const s=Math.max(800/img.width,480/img.height)*$('#zoom').value/100;return{w:img.width*s,h:img.height*s}}
function drawCrop(){if(!img)return;const g=$('#cropc').getContext('2d'),{w,h}=cropSize();
 const x=(800-w)*($('#offX').value/100),y=(480-h)*($('#offY').value/100);g.fillStyle='#000';g.fillRect(0,0,800,480);g.drawImage(img,x,y,w,h)}
// Prevlačenje slike prstom (van oznaka) dok se iseca
$('#stage').addEventListener('pointerdown',e=>{if(!img||e.target.closest('.mk'))return;const r=$('#stage').getBoundingClientRect();
 pan={x:e.clientX,y:e.clientY,ox:+$('#offX').value,oy:+$('#offY').value,k:800/r.width};try{$('#stage').setPointerCapture(e.pointerId)}catch(_){}});
document.addEventListener('pointermove',e=>{if(!pan||!img)return;const{w,h}=cropSize(),dx=(e.clientX-pan.x)*pan.k,dy=(e.clientY-pan.y)*pan.k;
 if(w>800)$('#offX').value=Math.max(0,Math.min(100,pan.ox-dx/(w-800)*100));if(h>480)$('#offY').value=Math.max(0,Math.min(100,pan.oy-dy/(h-480)*100));drawCrop()});
document.addEventListener('pointerup',()=>pan=null);
function toRgb565(){const d=$('#cropc').getContext('2d').getImageData(0,0,800,480).data,W=800,H=480;
 const e=new Float32Array(W*H*3);for(let i=0;i<W*H;i++){e[i*3]=d[i*4];e[i*3+1]=d[i*4+1];e[i*3+2]=d[i*4+2]}
 const out=new Uint16Array(W*H),L=[31,63,31];
 for(let y=0;y<H;y++)for(let x=0;x<W;x++){const i=y*W+x,q=[];
  for(let c=0;c<3;c++){const v=Math.max(0,Math.min(255,e[i*3+c])),qv=Math.round(v*L[c]/255),er=v-qv*255/L[c];q.push(qv);
   if(x+1<W)e[(i+1)*3+c]+=er*7/16;if(y+1<H){if(x>0)e[(i+W-1)*3+c]+=er*3/16;e[(i+W)*3+c]+=er*5/16;if(x+1<W)e[(i+W+1)*3+c]+=er/16}}
  out[i]=q[0]<<11|q[1]<<5|q[2]}
 return new Blob([out.buffer],{type:'application/octet-stream'})}
$('#send').onclick=()=>{if(!img)return;const bp=cropPg,blob=toRgb565(),fd=new FormData();fd.append('file',blob,'bg.rgb565');
 const x=new XMLHttpRequest(),pr=$('#prog');pr.style.display='block';$('#send').disabled=true;
 x.upload.onprogress=e=>{if(e.lengthComputable)pr.firstChild.style.width=(e.loaded/e.total*100)+'%'};
 x.onload=()=>{$('#send').disabled=false;pr.style.display='none';if(x.status==200){toast('Fotografija je na panelu');delete bgCache[bp];S.pages[bp].img=S.pages[bp].own=true;cancelCrop();renderPages();drawBg()}
  else toast(x.status==403?'Pogrešan PIN':x.responseText||'Greška',1)};
 x.onerror=()=>{$('#send').disabled=false;pr.style.display='none';toast('Prekinuta veza',1)};
 x.open('POST','/api/bg?page='+bp+'&pin='+encodeURIComponent(pin));x.send(fd)};
function renderBgBtn(){const p=S.pages[PG];$('#bgReset').textContent=PG==0?'Vrati ugrađenu':'Ukloni fotografiju';$('#bgReset').style.display=p&&p.own?'':'none'}
$('#bgReset').onclick=async()=>{const bp=PG;if(!confirm(bp==0?'Vratiti ugrađenu fotografiju?':'Ukloniti fotografiju ovog dela?'))return;
 try{await post('/api/bg/reset?page='+bp,{});toast(bp==0?'Vraćena ugrađena slika':'Fotografija je uklonjena');delete bgCache[bp];S.pages[bp].own=false;S.pages[bp].img=bp==0;renderPages();drawBg()}catch(e){toast(e.message,1)}};

// ---------- nalozi
function renderAcc(){$('#tuyaSt').textContent=stText(S.tuya);$('#ewSt').textContent=stText(S.ewelink);
 $('#tuyaEp').innerHTML=S.regions.map(r=>`<option value="${esc(r.e)}" ${r.e==S.cfg.tuyaEp?'selected':''}>${esc(r.l)}</option>`).join('');
 $('#tuyaId').value=S.cfg.tuyaId;$('#tuyaSecret').placeholder=S.cfg.tuyaSecret?'sačuvano — ostavi prazno':'';
 $('#ewId').value=S.cfg.ewId;$('#ewSecret').placeholder=S.cfg.ewSecret?'sačuvano — ostavi prazno':'';
 $('#redir').textContent=S.redirect;$('#ewLink').href='/ewelink/start?pin='+encodeURIComponent(pin);$('#ssid').value=S.wifi}
$('#saveT').onclick=async()=>{try{await post('/api/tuya',{ep:$('#tuyaEp').value,id:$('#tuyaId').value.trim(),secret:$('#tuyaSecret').value.trim()});$('#tuyaSecret').value='';toast('Tuya sačuvan');setTimeout(load,4000)}catch(e){toast(e.message,1)}};
$('#saveE').onclick=async()=>{try{await post('/api/ewelink',{id:$('#ewId').value.trim(),secret:$('#ewSecret').value.trim()});$('#ewSecret').value='';toast('eWeLink sačuvan');setTimeout(load,2000)}catch(e){toast(e.message,1)}};
$('#saveW').onclick=async()=>{if(!$('#ssid').value.trim())return toast('Unesi naziv mreže',1);if(!confirm('Panel će se povezati na novu mrežu. Nastaviti?'))return;
 try{await post('/api/wifi',{ssid:$('#ssid').value.trim(),pass:$('#wpass').value});toast('Panel se povezuje na novu mrežu')}catch(e){toast(e.message,1)}};

function showTab(t){const b=document.querySelector('#tabs button[data-t="'+t+'"]');if(!b)return;document.querySelectorAll('#tabs button,main section').forEach(x=>x.classList.remove('on'));b.classList.add('on');$('#'+t).classList.add('on')}
document.querySelectorAll('#tabs button').forEach(b=>b.onclick=()=>{showTab(b.dataset.t);history.replaceState(null,'','#'+b.dataset.t)});
if(location.hash)showTab(location.hash.slice(1));
async function poll(){if(document.hidden||!ON||!S||dirty||D)return;try{const n=await (await api('/api/state')).json();const f=a=>JSON.stringify([a.items.map(i=>[i.k,i.v,i.on,i.o,i.h]),a.layout,a.pages,a.scenes]);
 if(f(n)!=f(S)){S=n;renderLayout();renderScenes();renderDevices()}S.tuya=n.tuya;renderStatus()}catch(e){}}
setInterval(poll,15000);
document.addEventListener('visibilitychange',poll);  // povratak na stranicu: odmah proveri PIN i stanje
start();
</script></body></html>)HTML";
