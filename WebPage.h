#pragma once
#include <Arduino.h>
#include <pgmspace.h>

/* =========================================================================
 *  WebPage.h - single-page responsive UI served from PROGMEM.
 *
 *  Layout: the page is the dark room and the orb is the only light in it.
 *  The orb mirrors the real strip - same colour, same brightness, and the
 *  same animation periods ledTask() uses (6 s breathe, 3.2 s longing,
 *  8 s doze, 1.2 s heartbeat) - so it reads as a window onto the lamp
 *  rather than decoration. Tapping it is the power switch.
 *
 *  Tabs: Light / Mood / Send / Dates / Setup.
 *  Endpoints are unchanged: /api/state, control, mood, note, sound,
 *  vibrate, dates, settings, ota/check, ota/update, wifireset.
 * ========================================================================= */

static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#14090f"><title>Love Lamp</title>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Instrument+Serif:ital@0;1&display=swap">
<style>
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
:root{--ink:#14090f;--ink2:#1e1119;--line:#33202b;--rose:#ff5c8a;--ember:#f0954e;
--txt:#f7eaef;--mut:#a98a98;--lamp:255,92,138;--lvl:1;
--ser:"Instrument Serif",Georgia,"Times New Roman",serif;
--ui:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;color-scheme:dark}
body{margin:0;background:var(--ink);color:var(--txt);font:15px/1.5 var(--ui);
min-height:100vh;padding-bottom:44px;overflow-x:hidden}
.px{max-width:520px;margin:0 auto;padding:0 18px}

/* ---- header + orb ---------------------------------------------------- */
header{padding:22px 0 4px;text-align:center}
h1{font:400 34px/1 var(--ser);margin:0;letter-spacing:.4px}
h1 i{font-style:italic;color:var(--rose)}
#sub{color:var(--mut);font-size:12px;margin-top:7px;display:flex;gap:7px;
justify-content:center;align-items:center;flex-wrap:wrap}
#dot{width:7px;height:7px;border-radius:50%;background:#5b4350;flex:0 0 auto}
#dot.up{background:var(--rose);box-shadow:0 0 8px var(--rose)}
.orb{position:relative;width:170px;height:170px;margin:18px auto 0;cursor:pointer;
border-radius:50%;filter:brightness(calc(.4 + var(--lvl) * .6));transition:filter .3s}
.orb:focus-visible{outline:2px solid var(--rose);outline-offset:14px}
.glow,.ball{position:absolute;border-radius:50%;animation:breathe 6s ease-in-out infinite}
.glow{inset:-42%;background:radial-gradient(circle,rgba(var(--lamp),.5) 0,
rgba(var(--lamp),.13) 46%,transparent 70%)}
.ball{inset:0;background:radial-gradient(circle at 38% 30%,rgba(255,255,255,.6) 0,
rgba(var(--lamp),.98) 40%,rgba(var(--lamp),.42) 76%,rgba(0,0,0,.5) 100%);
box-shadow:inset 0 -20px 36px rgba(0,0,0,.5),0 0 70px rgba(var(--lamp),.3)}
.m-miss  .glow,.m-miss  .ball{animation-duration:3.2s}
.m-sleep .glow,.m-sleep .ball{animation-name:doze;animation-duration:8s}
.m-work  .glow,.m-work  .ball{animation:none;opacity:.8}
.m-hug   .glow,.m-hug   .ball{animation:thump 1.2s ease-in-out infinite}
.off .glow{opacity:0}
.off .ball{animation:none;filter:grayscale(1) brightness(.22)}
@keyframes breathe{0%,100%{opacity:.7}50%{opacity:1}}
@keyframes doze{0%,100%{opacity:.14}50%{opacity:.42}}
@keyframes thump{0%{opacity:.3}7%{opacity:1}17%{opacity:.45}25%{opacity:.88}38%,100%{opacity:.3}}
#cap{text-align:center;margin-top:16px}
#capM{font:400 20px/1.2 var(--ser)}
#capT{color:var(--mut);font-size:12px;margin-top:3px}
#buzz{margin:18px auto 4px;display:block;max-width:320px}

/* ---- nav ------------------------------------------------------------- */
nav{position:sticky;top:0;z-index:5;margin-top:22px;background:var(--ink)}
nav div{display:flex;gap:24px;max-width:520px;margin:0 auto;padding:12px 18px 0;
overflow-x:auto;scrollbar-width:none;border-bottom:1px solid var(--line)}
nav div::-webkit-scrollbar{display:none}
nav button{flex:0 0 auto;border:0;background:none;color:var(--mut);font:inherit;
font-size:13px;padding:0 0 11px;cursor:pointer;border-bottom:2px solid transparent;
margin-bottom:-1px}
nav button.on{color:var(--txt);border-bottom-color:var(--rose)}

/* ---- content --------------------------------------------------------- */
main{padding-top:22px}
section{display:none}section.on{display:block}
h2{font:400 13px/1 var(--ui);text-transform:uppercase;letter-spacing:1.6px;
color:var(--mut);margin:28px 0 12px}
section h2:first-of-type{margin-top:0}
.mini{font-size:12.5px;color:var(--mut);margin-top:9px}
code{font-family:ui-monospace,Menlo,Consolas,monospace;font-size:11.5px;color:var(--txt)}
label{display:block;font-size:12px;color:var(--mut);margin:16px 0 6px}
input,textarea,select{width:100%;background:var(--ink2);border:1px solid var(--line);
color:var(--txt);border-radius:11px;padding:11px;font:inherit}
input:focus,textarea:focus,select:focus{outline:0;border-color:var(--rose)}
textarea{resize:vertical}
.b{display:block;width:100%;border:0;border-radius:13px;padding:14px;font:inherit;
font-weight:600;background:var(--rose);color:#2a0a14;cursor:pointer;margin-top:14px}
.b.g{background:none;color:var(--txt);border:1px solid var(--line);font-weight:500}
.b:active{transform:scale(.985)}
.b[disabled]{opacity:.35;cursor:default}

/* colour swatches */
#sw{display:grid;grid-template-columns:repeat(auto-fit,minmax(46px,1fr));gap:11px}
#sw button{aspect-ratio:1;border-radius:50%;border:0;cursor:pointer;
box-shadow:0 0 0 1px rgba(255,255,255,.14) inset}
#sw button.on{box-shadow:0 0 0 2px var(--ink),0 0 0 4px var(--txt)}
.cust{display:flex;align-items:center;gap:12px;margin-top:16px}
.cust input{width:52px;height:40px;padding:3px;border-radius:11px;flex:0 0 auto}
.cust span{font-size:12.5px;color:var(--mut)}
input[type=range]{-webkit-appearance:none;appearance:none;padding:0;height:34px;
background:none;border:0}
input[type=range]::-webkit-slider-runnable-track{height:6px;border-radius:3px;
background:linear-gradient(90deg,var(--rose),var(--ember))}
input[type=range]::-moz-range-track{height:6px;border-radius:3px;
background:linear-gradient(90deg,var(--rose),var(--ember))}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:22px;height:22px;
border-radius:50%;background:#fff;margin-top:-8px;box-shadow:0 2px 6px #0008}
input[type=range]::-moz-range-thumb{width:22px;height:22px;border:0;border-radius:50%;
background:#fff;box-shadow:0 2px 6px #0008}

/* mood rows */
.mr{display:flex;align-items:center;gap:14px;width:100%;text-align:left;
background:none;border:0;border-bottom:1px solid var(--line);color:var(--txt);
font:inherit;padding:15px 2px;cursor:pointer}
.mr:last-child{border:0}
.mr b{width:15px;height:15px;border-radius:50%;flex:0 0 auto}
.mr i{font-style:normal;flex:1}
.mr strong{display:block;font-size:15px;font-weight:600}
.mr span{display:block;font-size:12.5px;color:var(--mut);margin-top:2px}
.mr.on strong{color:var(--rose)}
.mr em{font-style:normal;color:var(--rose);opacity:0}
.mr.on em{opacity:1}

/* notes + sounds */
.qk{display:flex;gap:9px;flex-wrap:wrap;margin-top:12px}
.qk button{border:1px solid var(--line);background:none;color:var(--mut);
border-radius:999px;padding:8px 14px;font:inherit;font-size:13px;cursor:pointer}
.qk button:active{color:var(--txt);border-color:var(--rose)}
#lastNote{font:italic 400 21px/1.45 var(--ser);color:var(--txt);
border-left:2px solid var(--rose);padding:2px 0 2px 16px}
.snd{display:grid;gap:10px}.snd .b{margin:0}

/* rows, dates, warnings */
.row{display:flex;gap:12px;align-items:center;justify-content:space-between;
padding:13px 2px;border-bottom:1px solid var(--line);font-size:14px}
.row:last-child{border:0}
.row .x{flex:0 0 auto;background:none;border:0;color:var(--mut);cursor:pointer;
font:inherit;font-size:13px;padding:4px}
.row .x:active{color:var(--rose)}
.d3{display:grid;grid-template-columns:2fr 1fr 1fr;gap:9px}
.d3 input{margin:0}
.warn{background:#3b1220;border:1px solid #ff5c7a;color:#ffdbe4;border-radius:12px;
padding:12px 14px;font-size:13px;margin-bottom:18px}
.cp{display:flex;gap:9px}
.cp input{font-family:ui-monospace,Menlo,Consolas,monospace;letter-spacing:.3px}
.cp button{width:auto;flex:0 0 auto;margin:0;padding:11px 16px}
#toast{position:fixed;left:50%;bottom:26px;transform:translate(-50%,90px);
background:#3b1424;border:1px solid var(--rose);color:var(--txt);padding:12px 20px;
border-radius:999px;font-size:13px;opacity:0;transition:.28s;pointer-events:none;
z-index:9;max-width:88vw;text-align:center}
#toast.on{opacity:1;transform:translate(-50%,0)}
@media (prefers-reduced-motion:reduce){*{animation:none!important;transition:none!important}}
</style></head><body>

<header class="px"><h1>Love <i>Lamp</i></h1>
<div id="sub"><span id="dot"></span><span id="subT">connecting&hellip;</span></div></header>

<div class="orb" id="orb" role="button" tabindex="0" aria-label="Turn the lamp on or off">
<div class="glow"></div><div class="ball"></div></div>
<div id="cap" class="px"><div id="capM">&nbsp;</div><div id="capT">&nbsp;</div></div>
<div class="px"><button class="b" id="buzz">Send a buzz</button></div>

<nav><div>
<button class="on" data-t="0">Light</button><button data-t="1">Mood</button>
<button data-t="2">Send</button><button data-t="3">Dates</button>
<button data-t="4">Setup</button>
</div></nav>

<main class="px">
<section class="on"><h2>Colour</h2><div id="sw"></div>
<div class="cust"><input type="color" id="col" aria-label="Custom colour">
<span>Pick any other colour</span></div>
<h2>Brightness</h2><input type="range" id="br" min="5" max="255" aria-label="Brightness">
<div class="mini">Both lamps follow along the moment you let go.</div></section>

<section><h2>How are you feeling</h2><div id="moods"></div></section>

<section><h2>Secret love note</h2>
<textarea id="note" rows="3" maxlength="96" placeholder="Type something sweet&hellip;"></textarea>
<div class="qk" id="quick"></div>
<button class="b" id="sendNote">Send it over</button>
<div class="mini">It chimes, buzzes, and sits on their little screen.</div>
<h2>Last note you received</h2><div id="lastNote">&mdash;</div>
<h2>Play on their lamp</h2><div class="snd">
<button class="b g" data-s="1">Romantic chime</button>
<button class="b g" data-s="2">Lullaby</button>
<button class="b g" data-s="3">Celebration song</button></div>
<h2>Volume of this lamp</h2><input type="range" id="vol" min="0" max="30" aria-label="Volume">
<div class="mini" id="volT">Yours only &mdash; they set their own. Worth turning
down before the lullaby plays at two in the morning.</div></section>

<section><h2>Days that matter</h2><div id="dl"></div>
<div class="mini" id="dEmpty">Nothing saved yet.</div>
<h2>Add one</h2>
<div class="d3"><input id="dn" placeholder="Name" maxlength="20">
<input id="dd" type="number" min="1" max="31" placeholder="DD">
<input id="dm" type="number" min="1" max="12" placeholder="MM"></div>
<button class="b g" id="dAdd">Add to the list</button>
<button class="b" id="dSave">Save to the lamp</button>
<div class="mini">Checked once a day. A match lights both lamps, plays the
celebration song and buzzes a heartbeat.</div>
<h2>Rehearsal</h2>
<button class="b g" id="test">Run the celebration now</button>
<div class="mini">Fires the whole routine on this lamp only, so you can check the
speaker, the motor and the strip on the day you build it &mdash; not on the day
that actually matters.</div></section>

<section><div class="warn" id="warn" hidden>This lamp can't sign in to Firebase.
Check its Wi-Fi, or its MAC address may already be registered under a
different device secret - open the app and remove the old entry.</div>
<h2>Pairing</h2>
<div class="row"><span>Status</span><span id="pStatus">&mdash;</span></div>
<div class="row"><span>Role</span><span id="pRole">&mdash;</span></div>
<div class="row"><span>MAC address</span><span id="pMac">&mdash;</span></div>
<div class="mini">Pairing happens in the Love Lamp web app, not here - register this
MAC address there, then create or join a pair. This page updates automatically
once that's done.</div>
<h2>Lamp name &amp; clock</h2>
<label>Lamp name (http://&lt;name&gt;.local)</label><input id="sName" maxlength="20">
<label>Time zone</label><input id="sTz" maxlength="38">
<div class="mini">Bangkok <code>ICT-7</code> &middot; Tokyo <code>JST-9</code> &middot;
London <code>GMT0BST,M3.5.0/1,M10.5.0</code> &middot;
New York <code>EST5EDT,M3.2.0,M11.1.0</code>.
Each lamp keeps its own, so you each celebrate on your own local date.</div>
<button class="b" id="sSave">Save &amp; reboot</button>
<h2>Lock this page</h2>
<input id="sPin" type="password" maxlength="15" placeholder="Set a PIN">
<div class="mini" id="pinT">Anyone on your Wi-Fi can open this page right now.
A PIN is asked once per browser; the username is <code>lamp</code>.</div>
<button class="b g" id="pinOff" hidden>Remove the PIN</button>
<h2>Firmware</h2>
<div class="row"><span>Installed</span><span id="fwCur">&mdash;</span></div>
<div class="row"><span>Available</span><span id="fwNew">&mdash;</span></div>
<div class="row"><span>Awake for</span><span id="fwUp">&mdash;</span></div>
<div class="row"><span>Free memory</span><span id="fwHeap">&mdash;</span></div>
<button class="b g" id="otaChk">Check for an update</button>
<button class="b" id="otaGo" disabled>Update now</button>
<div class="mini" id="otaMsg">Takes about 40 seconds. Keep the lamp powered.</div>
<h2>Starting over</h2>
<button class="b g" id="wifiRst">Forget Wi-Fi &amp; open setup portal</button>
<div class="mini">The setup portal is also where the sync server is configured.</div>
</section>
</main><div id="toast"></div><script>
const $=s=>document.querySelector(s),$$=s=>[...document.querySelectorAll(s)];
let S={},dates=[],binUrl="";
const PRE=["#ff5c8a","#ff8a5c","#ffc46b","#7ee0c0","#5cc8ff","#9d7bff","#ff4040","#ffeede"];
const MOODS=[["miss","Miss You","slow pink breathing","#ff3c8c"],
["sleep","Sleeping","dim amber, plays the lullaby","#ff781e"],
["work","Working","steady blue, no interruptions","#1e78ff"],
["hug","Need a Hug","red heartbeat pulse","#ff283c"],
["normal","Together","back to your own colour","#ff5c8a"]];
const QUICK=["Miss you","Good night","On my way","Thinking of you"];
const toast=m=>{const t=$("#toast");t.textContent=m;t.className="on";
clearTimeout(t._h);t._h=setTimeout(()=>t.className="",2200)};
const api=(p,o)=>fetch(p,o).then(r=>{if(!r.ok)throw 0;return r.json()});
const post=(p,b)=>api(p,{method:"POST",headers:{"Content-Type":"application/json"},
body:JSON.stringify(b)});
const hex=o=>"#"+[o.r,o.g,o.b].map(v=>("0"+v.toString(16)).slice(-2)).join("");

$("#sw").innerHTML=PRE.map(c=>`<button data-c="${c}" style="background:${c}" aria-label="${c}"></button>`).join("");
$("#moods").innerHTML=MOODS.map(m=>`<button class="mr" data-m="${m[0]}"><b style="background:${m[3]}"></b>
<i><strong>${m[1]}</strong><span>${m[2]}</span></i><em>&#10003;</em></button>`).join("");
$("#quick").innerHTML=QUICK.map(q=>`<button>${q}</button>`).join("");

$$("nav button").forEach(b=>b.onclick=()=>{
 $$("nav button").forEach(x=>x.className="");b.className="on";
 $$("section").forEach((s,i)=>s.className=i==+b.dataset.t?"on":"");
 window.scrollTo({top:0,behavior:"smooth"})});

const upt=s=>{const d=(s/86400)|0,h=(s%86400/3600)|0,m=(s%3600/60)|0;
 return(d?d+"d ":"")+(h?h+"h ":"")+m+"m"};
function paint(s){S=s;
 $("#dot").className=s.linked?"up":"";
 $("#subT").textContent=(s.paired?(s.linked?"together":"waiting for them"):"not paired yet")+
  (s.paired?" · role "+s.role:"")+" · "+(s.time||"no clock yet");
 document.documentElement.style.setProperty("--lamp",s.r+","+s.g+","+s.b);
 $("#orb").className="orb "+(s.power?"m-"+s.mood:"off");
 const md=MOODS.find(m=>m[0]==s.mood)||MOODS[4];
 $("#capM").textContent=s.power?md[1]:"Lamp is off";
 $("#capT").textContent=s.power?"tap the lamp to turn it off":"tap the lamp to wake it";
 const f=document.activeElement;
 if(f!=$("#col")){$("#col").value=hex(s)}
 if(f!=$("#br")){$("#br").value=s.bright;   // don't fight a finger on the slider
  document.documentElement.style.setProperty("--lvl",(s.bright/255).toFixed(2))}
 if(f!=$("#vol")){$("#vol").value=s.vol}
 $("#vol").disabled=!s.audio;
 if(!s.audio)$("#volT").textContent="No sound module answered on Serial2 — check the wiring.";
 $("#fwHeap").textContent=(s.heap/1024|0)+" KB free, largest block "+(s.block/1024|0)+" KB";
 $$("#sw button").forEach(b=>b.className=b.dataset.c==hex(s)?"on":"");
 $$(".mr").forEach(b=>b.className="mr"+(b.dataset.m==s.mood?" on":""));
 $("#lastNote").textContent=s.note||"nothing yet";
 $("#fwCur").textContent=s.fw;$("#fwUp").textContent=upt(s.uptime);
 $("#pMac").textContent=s.mac||"—";
 $("#pRole").textContent=s.paired?s.role:"—";
 $("#pStatus").textContent=s.authTrouble?"can't sign in to Firebase":
  s.paired?"paired":s.claimed?"registered — create or join a pair in the app":
  "unregistered — register this MAC in the app";
 $("#warn").hidden=!s.authTrouble}
const load=()=>api("/api/state").then(paint).catch(()=>$("#subT").textContent="lamp offline");

const toggle=()=>post("/api/control",{power:!S.power}).then(paint);
$("#orb").onclick=toggle;
$("#orb").onkeydown=e=>{if(e.key=="Enter"||e.key==" "){e.preventDefault();toggle()}};
const apply=t=>post("/api/control",{color:$("#col").value,brightness:+$("#br").value})
 .then(s=>{paint(s);if(t)toast(t)});
$$("#sw button").forEach(b=>b.onclick=()=>{$("#col").value=b.dataset.c;apply()});
$("#col").onchange=()=>apply();
$("#br").oninput=function(){document.documentElement.style
 .setProperty("--lvl",(this.value/255).toFixed(2))};
$("#br").onchange=()=>apply();
$("#buzz").onclick=()=>post("/api/vibrate",{target:"partner"})
 .then(()=>toast("They just felt that"));
$$(".mr").forEach(b=>b.onclick=()=>post("/api/mood",{mood:b.dataset.m})
 .then(s=>{paint(s);toast("They can see how you feel")}));
$$("#quick button").forEach(b=>b.onclick=()=>{$("#note").value=b.textContent;$("#note").focus()});
$("#sendNote").onclick=()=>{const v=$("#note").value.trim();
 if(!v)return toast("Write something first");
 post("/api/note",{note:v}).then(s=>{paint(s);$("#note").value="";toast("Note sent")})};
$$("[data-s]").forEach(b=>b.onclick=()=>post("/api/sound",{track:+b.dataset.s,target:"partner"})
 .then(()=>toast("Playing on their lamp")));
$("#vol").onchange=function(){post("/api/control",{volume:+this.value})
 .then(s=>{paint(s);toast("Volume "+s.vol)})};

function drawDates(){$("#dl").innerHTML=dates.map((d,i)=>
 `<div class="row"><span>${d.name.replace(/[<>&]/g,"")} &nbsp;<span style="color:var(--mut)">`+
 `${("0"+d.day).slice(-2)}/${("0"+d.month).slice(-2)}</span></span>`+
 `<button class="x" data-i="${i}">Remove</button></div>`).join("");
 $("#dEmpty").style.display=dates.length?"none":"block";
 $$("#dl .x").forEach(b=>b.onclick=()=>{dates.splice(+b.dataset.i,1);drawDates()})}
const loadDates=()=>api("/api/dates").then(d=>{dates=d.dates||[];drawDates()});
$("#dAdd").onclick=()=>{const n=$("#dn").value.trim(),d=+$("#dd").value,m=+$("#dm").value;
 if(!n||!(d>=1&&d<=31)||!(m>=1&&m<=12))return toast("Needs a name, a day and a month");
 if(dates.length>=12)return toast("Twelve is the limit");
 dates.push({name:n,day:d,month:m});$("#dn").value=$("#dd").value=$("#dm").value="";
 drawDates();toast("Added — now save it")};
$("#dSave").onclick=()=>post("/api/dates",{dates}).then(()=>toast("Saved to the lamp"));
$("#test").onclick=()=>post("/api/testalert",{}).then(()=>toast("Watch the lamp"));

const loadCfg=()=>api("/api/settings").then(c=>{
 $("#sName").value=c.name;$("#sTz").value=c.tz;$("#pinOff").hidden=!c.lock;
 if(c.lock)$("#pinT").innerHTML="This page is locked. Type a new PIN to change it, "+
  "or remove it below. Forget it and the lamp needs a USB re-flash."});
$("#sSave").onclick=()=>{const b={name:$("#sName").value,tz:$("#sTz").value};
 if($("#sPin").value)b.pin=$("#sPin").value;   // absent = leave the PIN alone
 post("/api/settings",b).then(()=>toast("Saved — rebooting"))};
$("#pinOff").onclick=()=>post("/api/settings",{pin:""})
 .then(()=>toast("PIN removed — rebooting"));
$("#otaChk").onclick=()=>{$("#otaMsg").textContent="Asking GitHub…";
 api("/api/ota/check").then(r=>{
  $("#fwCur").textContent=r.current;$("#fwNew").textContent=r.latest||"—";binUrl=r.url||"";
  $("#otaGo").disabled=!r.available;
  $("#otaMsg").textContent=r.available?(r.notes||"A new version is ready.")
   :(r.error?"Could not check: "+r.error:"You are on the latest version.")})
 .catch(()=>$("#otaMsg").textContent="Could not reach GitHub.")};
$("#otaGo").onclick=()=>{$("#otaMsg").textContent="Downloading and flashing — do not unplug.";
 post("/api/ota/update",{url:binUrl}).catch(()=>{});
 setTimeout(()=>$("#otaMsg").textContent="Rebooting — reload this page in a minute.",4000)};
$("#wifiRst").onclick=()=>{if(!confirm("Forget Wi-Fi and reopen the setup portal?"))return;
 post("/api/wifireset",{}).catch(()=>{});toast("Rebooting into setup")};
/* Poll quickly while a hand is on the page, back off when it is idle, stop
   entirely when the tab is hidden. Still polling, though: instant push needs
   SSE, which needs an async web server. */
let quickUntil=Date.now()+3e4;
const busy=()=>quickUntil=Date.now()+3e4;
addEventListener("pointerdown",busy);addEventListener("keydown",busy);
const tick=()=>{if(!document.hidden)load();
 setTimeout(tick,document.hidden?8000:(Date.now()<quickUntil?1500:6000))};
document.addEventListener("visibilitychange",()=>{if(!document.hidden){busy();load()}});
load();loadDates();loadCfg();setTimeout(tick,1500);
</script></body></html>)HTML";
