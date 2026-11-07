#include "web_server.h"

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "pixels_ble.h"
#include "power.h"
#include "preferences.h"
#include "status_led.h"
#include "theme.h"

namespace app {
namespace {

constexpr char kTag[] = "pixels_web";
constexpr char kAccessPointSsid[] = "Pixels-Dice";
constexpr char kAccessPointPassword[] = "pixelsdice";
constexpr char kAccessPointAddress[] = "10.47.1.1";

DiceModel *model;
httpd_handle_t server;
std::atomic_bool station_enabled;
std::atomic<NetworkStatus> network_status{NetworkStatus::kOff};
std::atomic<uint32_t> station_address{0};
bool wifi_initialized;
std::atomic_bool wifi_started;
std::atomic_bool sse_active;
std::atomic_bool sse_shutdown;
std::atomic_int sse_socket{-1};
SemaphoreHandle_t sse_stopped;

constexpr char kIndexHtml[] = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#0b1020">
<title>Pixels Dice Hub</title>
<style>
:root{color-scheme:dark;--bg:#0b1020;--panel:#172033;--text:#f8fafc;--on-accent:#f8fafc;--muted:#94a3b8;--dimmed:#334155;
--accent:#2563eb;--accent-text:#bfdbfe;--accent-alt:#1d4ed8;--button:#334155;--danger:#f87171;--danger-bg:#991b1b;
--success:#4ade80;--success-bg:#166534;--warning:#f59e0b;--info:#38bdf8;--violet:#a78bfa;--stale:#64748b;--border:#334155}
:root.light{color-scheme:light}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px system-ui,sans-serif}
main{max-width:1000px;margin:auto;padding:clamp(10px,3vw,20px)}.top{display:flex;align-items:center;justify-content:space-between;gap:10px;flex-wrap:wrap}
h1,h2,p{margin:0}h2{font-size:17px}.muted{color:var(--muted)}button,input,select{font:inherit;border-radius:8px}
button{border:0;background:var(--button);color:var(--on-accent);padding:9px 12px;min-height:40px;cursor:pointer;touch-action:manipulation}
button:disabled{cursor:not-allowed;opacity:.45}.accent{background:var(--accent);color:var(--on-accent)}
.calc-controls{display:flex;flex-wrap:wrap;gap:8px;margin-top:10px;align-items:center}.calc-controls button{min-width:42px}
.num{display:inline-flex;align-items:center;gap:8px;white-space:nowrap}
.calc-result{font-size:clamp(26px,6vw,36px);font-weight:700;color:var(--success);margin-top:8px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:10px;margin:14px 0}
.card{background:var(--panel);border:1px solid var(--border);border-radius:12px;padding:12px;min-height:120px}
.die{cursor:pointer;border-color:var(--die,var(--info))}.die.offline{opacity:.55}.die-head{display:flex;justify-content:space-between;align-items:center;gap:6px}
.die-head b{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.die.rolling{border-color:var(--info);box-shadow:0 0 14px color-mix(in srgb,var(--info) 40%,transparent)}.die.handling{border-color:var(--warning)}
.die.crooked{border-color:var(--danger);box-shadow:0 0 14px color-mix(in srgb,var(--danger) 40%,transparent)}.die.rolled{border-color:var(--success)}
.die.rolling .die-state{color:var(--info)}.die.handling .die-state{color:var(--warning)}.die.crooked .die-state{color:var(--danger)}
.die.rolled .die-state{color:var(--success)}.die-state{font-size:12px;font-weight:700;color:var(--muted);white-space:nowrap}
.die.rolling .roll,.die.handling .roll{color:var(--dimmed)}
.battery{position:relative;display:inline-block;flex:none;width:22px;height:11px;border:1px solid var(--muted);border-radius:3px;padding:1px}
.battery::after{content:"";position:absolute;right:-4px;top:2px;width:2px;height:5px;background:var(--muted);border-radius:0 1px 1px 0}
.battery i{display:block;height:100%;background:var(--muted);border-radius:1px}.battery.low i{background:var(--danger)}
.battery.warn i{background:var(--warning)}.battery.charging i{background:var(--success)}
.hub-low{color:var(--danger)}.hub-warn{color:var(--warning)}.hub-usb{color:var(--success)}
.roll{font-size:clamp(40px,9vw,52px);font-weight:700;text-align:center;line-height:1.25;text-shadow:
-1px -1px var(--bg),1px -1px var(--bg),-1px 1px var(--bg),1px 1px var(--bg)}
.aggregate-row{display:grid;grid-template-columns:1fr auto;gap:8px}.aggregate{background:var(--accent);color:var(--on-accent);display:flex;
align-items:center;justify-content:space-between;min-height:84px;width:100%;border-color:transparent}.aggregate strong{font-size:clamp(34px,8vw,44px)}
.aggregate small{color:var(--accent-text)}
.clear{background:var(--danger-bg);color:var(--on-accent);min-width:76px;font-weight:700}.mode-detail{display:flex;flex-direction:column;align-items:flex-start}
.section{margin-top:20px}.row{display:flex;gap:10px;align-items:center;padding:9px 0;border-bottom:1px solid var(--border)}
.row .grow{flex:1;min-width:0}.row .grow b{display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.history-value{font-size:24px;font-weight:700;min-width:48px;text-align:right}
form{display:grid;grid-template-columns:1fr 1fr auto;gap:8px;margin-top:10px}
input,select{min-width:0;border:1px solid var(--border);background:var(--bg);color:var(--text);padding:9px;min-height:40px}
.settings{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:10px;margin-top:10px}
.setting{display:flex;align-items:center;justify-content:space-between;gap:10px;background:var(--panel);border:1px solid var(--border);border-radius:10px;padding:10px 12px}
.setting label,.setting b{font-weight:600}.setting select,.setting button{flex:none}
.danger{background:var(--danger-bg);color:var(--on-accent)}.outline{background:transparent;border:1px solid var(--danger);color:var(--danger)}
.view[hidden],form[hidden]{display:none}.icon{font-size:22px;line-height:1;padding:6px 12px;min-width:44px}
.menu{display:grid;gap:8px;margin-top:14px}.menu>button{width:100%;min-height:44px;font-weight:600;text-align:left;padding:10px 14px}
.menu .setting{min-height:44px}.menu form{margin:0}.wifi .grow{min-width:0}.wifi b{display:block}.wifi .muted{font-size:13px;word-break:break-all}
.empty{color:var(--muted);padding:20px;text-align:center}.status{font-size:12px;color:var(--muted)}
.modal{position:fixed;inset:0;background:#000a;display:flex;align-items:center;justify-content:center;padding:16px;z-index:10}
.modal[hidden]{display:none}.modal-card{background:var(--bg);border:1px solid var(--border);border-radius:14px;padding:16px;
width:min(460px,100%);max-height:85vh;overflow:auto}.modal-head{display:flex;align-items:center;justify-content:space-between;margin-bottom:8px;gap:10px}
.die-info{white-space:pre-line;margin:8px 0}
@media(max-width:560px){form{grid-template-columns:1fr}.grid{grid-template-columns:repeat(2,1fr)}
.card{min-height:105px}.aggregate-row{grid-template-columns:1fr}.clear{min-height:44px}}
@media(max-width:360px){.grid{grid-template-columns:1fr}}
</style>
</head>
<body><main>
<div id="home" class="view">
<div class="top"><div><h1>PIXELS</h1><div id="status" class="status">Connecting...</div></div>
<button class="icon" onclick="showView('settings')" title="Settings" aria-label="Settings">&#9881;</button></div>
<div id="dice" class="grid"></div>
<div class="aggregate-row"><button id="aggregate" class="card aggregate" onclick="action('cycle')">
<span class="mode-detail"><span id="mode">SUM</span><small id="roll-count">0 rolls</small></span><strong id="total">-</strong></button>
<button class="clear" onclick="action('clear')">CLEAR</button></div>
<section class="section card"><h2>Guided calculator</h2>
<div class="calc-controls"><button id="calc-prev" onclick="action('calcPrev')">‹</button>
<button id="calc-preset" onclick="action('calcNext')">D20 CHECK ›</button></div>
<div id="calc-config" class="calc-controls"></div>
<p id="calc-instruction" class="muted" style="margin-top:10px"></p>
<div id="calc-result" class="calc-result"></div>
<div class="calc-controls"><button id="calc-start" onclick="action('calcStart')">Start round</button>
<button onclick="action('calcCancel')">Cancel</button></div></section>
<section class="section"><h2>Last 20 rolls</h2><div id="history"></div></section>
</div>
<div id="settings" class="view" hidden>
<div class="top"><h1>SETTINGS</h1><button class="accent" onclick="showView('home')">Done</button></div>
<div class="menu">
<button class="accent" onclick="showView('pair')">Pair dice ›</button>
<div class="setting wifi"><div class="grow"><b>Wi-Fi <span id="net-status"></span></b><div id="net-address" class="muted"></div></div>
<button onclick="toggleWifiForm()">Join network</button></div>
<form id="wifi" hidden><input name="ssid" maxlength="32" placeholder="Network name">
<input name="password" maxlength="63" type="password" placeholder="Password">
<button type="submit">Save</button></form>
<div id="lighting" class="setting" hidden><label>Status light</label><button id="led-mode" onclick="action('ledMode')">Status light</button></div>
<div class="setting"><label for="theme">Theme</label><select id="theme" onchange="setValue('theme',this.value)"></select></div>
<div id="sleep-setting" class="setting" hidden><label for="sleep">Auto sleep</label><select id="sleep" onchange="setValue('sleep',this.value)"></select></div>
<div id="power-setting" class="setting" hidden><label>Power</label><button class="danger" onclick="confirmAction('Power off the hub now?','powerOff')">Power off</button></div>
<div class="setting"><label>Reset</label><button class="outline" onclick="confirmAction('Factory reset erases paired dice, Wi-Fi and all settings. Continue?','factoryReset')">Factory reset</button></div>
</div>
<p id="sleep-note" class="muted" style="margin-top:10px"></p>
</div>
<div id="pair" class="view" hidden>
<div class="top"><h1>PAIR DICE</h1><button onclick="showView('settings')">Back</button></div>
<p class="muted">Paired dice stay listed; nearby unpaired Pixels appear below them. Handle a die to wake it.</p>
<div id="pairing"></div>
</div>
</main>
<div id="die-modal" class="modal" hidden onclick="closeDieHistory()"><div class="modal-card" onclick="event.stopPropagation()">
<div class="modal-head"><div><h2 id="die-title">Die history</h2><div id="die-status" class="muted"></div>
<div id="die-info" class="muted die-info"></div><div id="die-subtitle" class="muted"></div></div>
<button onclick="closeDieHistory()">Close</button></div><div id="die-history"></div>
<button onclick="loadDieHistory()" style="width:100%;margin-top:8px">Refresh</button></div></div>
<script>
function batteryClass(p,low,charging){return charging?'charging':(low||p<15)?'low':p<=25?'warn':''}
const colors=['#8b5cf6','#38bdf8','#f59e0b','#22c55e','#ec4899','#06b6d4','#f97316','#84cc16'];
const esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
async function action(op,id){await fetch('/api/action?op='+op+(id?'&id='+id:''),{method:'POST'});refresh()}
async function setValue(op,value){await fetch('/api/action?op='+op+'&value='+value,{method:'POST'});refresh()}
async function confirmAction(text,op){if(!confirm(text))return;const r=await fetch('/api/action?op='+op,{method:'POST'});
if(!r.ok)alert(await r.text());else document.querySelector('#status').textContent=op==='powerOff'?'Powering off...':'Resetting...'}
let themeIndex=-1;
function applyTheme(t){if(t.index===themeIndex)return;themeIndex=t.index;const c=t.colors,root=document.documentElement;
for(const k in c)root.style.setProperty('--'+k.replace(/[A-Z]/g,m=>'-'+m.toLowerCase()),c[k]);
root.style.setProperty('--border',c.dimmed);root.classList.toggle('light',t.light);
colors.splice(0,colors.length,...t.tiles);document.querySelector('meta[name=theme-color]').content=c.bg;
const sel=document.querySelector('#theme');sel.innerHTML=t.names.map((n,i)=>`<option value="${i}">${esc(n)}</option>`).join('');sel.value=t.index}
function renderSettings(s){applyTheme(s.theme);const sleep=document.querySelector('#sleep');
sleep.innerHTML=s.idleSleep.options.map(m=>`<option value="${m}">${m?m+' min':'Off'}</option>`).join('');sleep.value=s.idleSleep.minutes;
document.querySelector('#sleep-setting').hidden=document.querySelector('#power-setting').hidden=!s.powerOff.supported;
document.querySelector('#sleep-note').textContent=s.powerOff.supported?'Auto sleep only runs on battery; it is paused while a charger is connected.':'';
document.querySelector('#net-status').textContent=s.network.status;
document.querySelector('#net-address').textContent=`http://${s.network.address} · the Pixels-Dice hotspot stays available`;
const lighting=document.querySelector('#lighting');lighting.hidden=!s.statusLed.supported;
if(s.statusLed.supported)document.querySelector('#led-mode').textContent=`${s.statusLed.mode} · ${s.statusLed.state}`}
const views=['home','settings','pair'];
function showView(name){if(!views.includes(name))name='home';for(const v of views)document.querySelector('#'+v).hidden=v!==name;
if(location.hash!==(name==='home'?'':'#'+name))history.pushState(null,'',name==='home'?location.pathname:'#'+name);window.scrollTo(0,0)}
window.addEventListener('popstate',()=>showView(location.hash.slice(1)||'home'));
function toggleWifiForm(){const f=document.querySelector('#wifi');f.hidden=!f.hidden;if(!f.hidden)f.ssid.focus()}
function blink(id){if(id)fetch('/api/action?op=blink&id='+id,{method:'POST'})}
function inspect(id){if(id)fetch('/api/action?op=info&id='+id,{method:'POST'})}
function dieCard(d,i){const stateClass=d.state.toLowerCase().replaceAll(' ','-');return `<article class="card die ${stateClass} ${d.offline?'offline':''}" style="--die:${colors[i%colors.length]}" onclick="showDieHistory('${d.id}')">
<div class="die-head"><b>${esc(d.type)}</b><span class="battery ${batteryClass(d.battery,d.batteryState==='low',d.charging)}" title="${d.battery}%"><i style="width:${Math.max(0,Math.min(100,d.battery))}%"></i></span></div>
<div class="roll">${d.hasRoll?d.roll:'-'}</div><div class="die-head"><span class="muted">${esc(d.name||('Pixel '+d.id))}</span>
<span class="die-state">${d.offline?'OFFLINE':esc(d.state)}</span></div></article>`}
let refreshTimer,refreshing=false,currentDice=[],selectedDieId='',eventsConnected=false;
function updateDieStatus(){const d=currentDice.find(d=>d.id===selectedDieId);if(!d)return;
document.querySelector('#die-title').textContent=`${d.type} ${d.name||'Pixel'}`;
document.querySelector('#die-status').textContent=`Battery ${d.battery}% · ${d.batteryState} · ${d.offline?'offline':d.state} · ${d.rssi} dBm`;
const details=[];if(d.hasConnectedInfo){details.push(`Firmware ${d.firmwareVersion||'legacy'} · build ${d.firmwareTimestamp}`);
details.push(`Profile ${d.profileHash} · ${d.availableFlash} bytes free`)}
if(d.mcuTemperature!==null)details.push(`MCU ${d.mcuTemperature.toFixed(2)} °C · battery ${d.batteryTemperature.toFixed(2)} °C`);
document.querySelector('#die-info').textContent=details.join('\n')||'Connected details not refreshed yet'}
async function loadDieHistory(){const requestedId=selectedDieId;if(!requestedId)return;const r=await fetch('/api/history?id='+requestedId,{cache:'no-store'});
const h=await r.json();if(requestedId!==selectedDieId)return;updateDieStatus();
document.querySelector('#die-subtitle').textContent=`${h.history.length} of the last 20 rolls`;
document.querySelector('#die-history').innerHTML=h.history.map(x=>`<div class="row"><div class="grow muted">${Math.floor(x.ageMs/1000)}s ago</div><div class="history-value">${x.value}</div></div>`).join('')||'<div class="empty">No completed rolls yet</div>'}
function showDieHistory(id){selectedDieId=id;document.querySelector('#die-modal').hidden=false;blink(id);inspect(id);loadDieHistory()}
function closeDieHistory(){blink(selectedDieId);selectedDieId='';document.querySelector('#die-modal').hidden=true}
function calcButton(label,op){return `<button onclick="action('${op}')">${esc(label)}</button>`}
function renderCalculator(c){document.querySelector('#calc-preset').textContent=c.preset+' ›';
const rolling=c.status==='ROLLING';document.querySelector('#calc-prev').disabled=rolling;
document.querySelector('#calc-preset').disabled=rolling;
const controls=[];const number=(label,value,down,up)=>controls.push(`<span class="num"><span class="muted">${esc(label)}</span>${calcButton('−',down)}<b>${value}</b>${calcButton('+',up)}</span>`);
if(!rolling&&c.preset==='D20 CHECK'){number('Modifier',c.modifier,'calcModDown','calcModUp');
controls.push(calcButton(c.dcEnabled?`DC ${c.dc}`:'DC off','calcDcToggle'));if(c.dcEnabled)number('DC',c.dc,'calcDcDown','calcDcUp')}
else if(!rolling&&(c.preset==='ADVANTAGE'||c.preset==='DISADVANTAGE'))number('Modifier',c.modifier,'calcModDown','calcModUp');
else if(!rolling){controls.push(`<span class="muted">Dice</span>${calcButton(c.poolDie,'calcDie')}`);number('Rolls',c.poolCount,'calcPoolDown','calcPoolUp');
if(c.preset==='SUCCESS POOL')number('Success on',c.threshold,'calcThresholdDown','calcThresholdUp');
else if(c.preset==='KEEP HIGHEST'||c.preset==='KEEP LOWEST')number('Keep',c.keepCount,'calcKeepDown','calcKeepUp');
else{number('Modifier',c.modifier,'calcModDown','calcModUp');controls.push(calcButton(c.critical?'Critical on':'Critical off','calcCrit'))}}
document.querySelector('#calc-config').innerHTML=controls.join('');
document.querySelector('#calc-instruction').textContent=c.instruction+(c.status==='ROLLING'?` (${c.completedRolls}/${c.totalRolls})`:'');
document.querySelector('#calc-result').textContent=c.hasResult?c.resultText:'';
const start=document.querySelector('#calc-start');start.disabled=rolling;
start.textContent=rolling?'Rolling…':(c.status==='COMPLETE'?'Roll again':'Start round')}
async function refresh(){if(refreshing)return;refreshing=true;try{const r=await fetch('/api/state',{cache:'no-store'});const s=await r.json();
currentDice=s.dice;updateDieStatus();const active=s.dice.filter(d=>d.active);
const b=s.battery;const hub=b&&b.supported&&b.valid?` · <span class="hub-${b.externalPower?'usb':b.low?'low':b.percent<=25?'warn':'ok'}">hub ${b.percent}%${b.externalPower?' ⚡':''}</span>`:'';
document.querySelector('#status').innerHTML=`${active.length} active · ${s.dice.length} paired · ${s.candidates.length} found${hub}`;
document.querySelector('#dice').innerHTML=active.length?active.map(dieCard).join(''):`<div class="card empty">No active dice<br><small>${s.dice.length} paired · ${s.candidates.length} found</small><br>Handle a die or pair one in <a href="#settings" onclick="showView('pair');return false">settings</a></div>`;
document.querySelector('#mode').textContent=s.aggregate.mode+' ›';document.querySelector('#total').textContent=s.aggregate.hasValue?s.aggregate.value:'-';
document.querySelector('#roll-count').textContent=s.aggregate.rollCount+' roll'+(s.aggregate.rollCount===1?'':'s');
renderCalculator(s.calculator);renderSettings(s);
const paired=s.dice.map(d=>`<div class="row"><div class="grow"><b>${esc(d.type)} ${esc(d.name)}</b><div class="muted">${d.id}</div></div><button onclick="action('unpair','${d.id}')">Remove</button></div>`);
const found=s.candidates.map(d=>`<div class="row"><div class="grow"><b>${esc(d.type)} ${esc(d.name)}</b><div class="muted">${d.id} · ${d.rssi} dBm</div></div><button onclick="action('pair','${d.id}')">Add</button></div>`);
document.querySelector('#pairing').innerHTML=[...paired,...found].join('')||'<div class="empty">No Pixels discovered yet</div>';
document.querySelector('#history').innerHTML=s.history.map(h=>`<div class="row"><div class="grow"><b>${esc(h.type)} ${esc(h.name||('Pixel '+h.id))}</b><div class="muted">${Math.floor(h.ageMs/1000)}s ago</div></div><div class="history-value">${h.value}</div></div>`).join('')||'<div class="empty">No completed rolls yet</div>';
}catch(e){document.querySelector('#status').textContent='Reconnecting...'}finally{refreshing=false}}
function scheduleFallback(){clearTimeout(refreshTimer);refreshTimer=setTimeout(async()=>{await refresh();scheduleFallback()},eventsConnected?15000:750)}
if(window.EventSource){const events=new EventSource('/api/events');
events.onopen=()=>{eventsConnected=true;scheduleFallback()};
events.addEventListener('state',()=>refresh());
events.onerror=()=>{eventsConnected=false;refresh();scheduleFallback()}}
document.querySelector('#wifi').addEventListener('submit',async e=>{e.preventDefault();const b=new URLSearchParams(new FormData(e.target));
const r=await fetch('/api/wifi',{method:'POST',body:b});alert(await r.text());e.target.reset();e.target.hidden=true});
showView(location.hash.slice(1)||'home');refresh();scheduleFallback();
</script></body></html>)HTML";

bool IsLightTheme(const Theme &theme) {
  const uint32_t bg = theme.background;
  const uint32_t luma = ((bg >> 16) & 0xff) * 299 + ((bg >> 8) & 0xff) * 587 +
                        (bg & 0xff) * 114;
  return luma > 128000;
}

void SendTheme(httpd_req_t *request) {
  const Theme &theme = CurrentTheme();
  char buffer[640];
  int n = std::snprintf(
      buffer, sizeof(buffer),
      ",\"theme\":{\"index\":%u,\"name\":\"%s\",\"light\":%s,\"colors\":{"
      "\"bg\":\"#%06lx\",\"panel\":\"#%06lx\",\"text\":\"#%06lx\","
      "\"onAccent\":\"#%06lx\",\"muted\":\"#%06lx\",\"dimmed\":\"#%06lx\","
      "\"accent\":\"#%06lx\",\"accentText\":\"#%06lx\",\"accentAlt\":\"#%06lx\","
      "\"button\":\"#%06lx\",\"danger\":\"#%06lx\",\"dangerBg\":\"#%06lx\","
      "\"success\":\"#%06lx\",\"successBg\":\"#%06lx\",\"warning\":\"#%06lx\","
      "\"info\":\"#%06lx\",\"violet\":\"#%06lx\",\"stale\":\"#%06lx\"},\"tiles\":[",
      static_cast<unsigned>(CurrentThemeIndex()), theme.name,
      IsLightTheme(theme) ? "true" : "false",
      static_cast<unsigned long>(theme.background),
      static_cast<unsigned long>(theme.panel),
      static_cast<unsigned long>(theme.text),
      static_cast<unsigned long>(theme.on_accent),
      static_cast<unsigned long>(theme.muted),
      static_cast<unsigned long>(theme.dimmed),
      static_cast<unsigned long>(theme.accent),
      static_cast<unsigned long>(theme.accent_text),
      static_cast<unsigned long>(theme.accent_alt),
      static_cast<unsigned long>(theme.button),
      static_cast<unsigned long>(theme.danger),
      static_cast<unsigned long>(theme.danger_bg),
      static_cast<unsigned long>(theme.success),
      static_cast<unsigned long>(theme.success_bg),
      static_cast<unsigned long>(theme.warning),
      static_cast<unsigned long>(theme.info),
      static_cast<unsigned long>(theme.violet),
      static_cast<unsigned long>(theme.stale));
  for (std::size_t i = 0; i < 8; ++i) {
    n += std::snprintf(buffer + n, sizeof(buffer) - n, "%s\"#%06lx\"",
                       i ? "," : "",
                       static_cast<unsigned long>(theme.tile_accents[i]));
  }
  n += std::snprintf(buffer + n, sizeof(buffer) - n, "],\"names\":[");
  for (std::size_t i = 0; i < kThemeCount; ++i) {
    n += std::snprintf(buffer + n, sizeof(buffer) - n, "%s\"%s\"",
                       i ? "," : "", ThemeAt(i).name);
  }
  std::snprintf(buffer + n, sizeof(buffer) - n, "]}");
  httpd_resp_sendstr_chunk(request, buffer);
}

void SendHubSettings(httpd_req_t *request) {
  SendTheme(request);
  char buffer[224];
  int n = std::snprintf(buffer, sizeof(buffer),
                        ",\"idleSleep\":{\"minutes\":%u,\"name\":\"%s\","
                        "\"options\":[",
                        IdleSleepMinutes(), IdleSleepName());
  bool first = true;
  for (uint8_t option : kIdleSleepChoices) {
    n += std::snprintf(buffer + n, sizeof(buffer) - n, "%s%u",
                       first ? "" : ",", option);
    first = false;
  }
  char address[16];
  std::snprintf(buffer + n, sizeof(buffer) - n,
                "]},\"powerOff\":{\"supported\":%s},"
                "\"network\":{\"status\":\"%s\",\"address\":\"%s\"}",
                IsPowerOffSupported() ? "true" : "false",
                NetworkStatusName(network_status.load()),
                GetWebAddress(address, sizeof(address)));
  httpd_resp_sendstr_chunk(request, buffer);
}

const char *BatteryStateName(const Die &die) {
  switch (die.battery_state) {
  case 1:
    return "low";
  case 2:
    return "charging";
  case 3:
    return "charged";
  case 4:
    return "charging position";
  case 5:
    return "battery error";
  default:
    return die.charging ? "charging" : "normal";
  }
}

void SendJsonString(httpd_req_t *request, const char *value) {
  httpd_resp_sendstr_chunk(request, "\"");
  char escaped[7] = {};
  for (const unsigned char *cursor =
           reinterpret_cast<const unsigned char *>(value);
       *cursor != '\0'; ++cursor) {
    switch (*cursor) {
    case '"':
      httpd_resp_sendstr_chunk(request, "\\\"");
      break;
    case '\\':
      httpd_resp_sendstr_chunk(request, "\\\\");
      break;
    case '\b':
      httpd_resp_sendstr_chunk(request, "\\b");
      break;
    case '\f':
      httpd_resp_sendstr_chunk(request, "\\f");
      break;
    case '\n':
      httpd_resp_sendstr_chunk(request, "\\n");
      break;
    case '\r':
      httpd_resp_sendstr_chunk(request, "\\r");
      break;
    case '\t':
      httpd_resp_sendstr_chunk(request, "\\t");
      break;
    default:
      if (*cursor < 0x20) {
        std::snprintf(escaped, sizeof(escaped), "\\u%04x", *cursor);
        httpd_resp_sendstr_chunk(request, escaped);
      } else {
        char character[2] = {static_cast<char>(*cursor), '\0'};
        httpd_resp_sendstr_chunk(request, character);
      }
    }
  }
  httpd_resp_sendstr_chunk(request, "\"");
}

void SendDie(httpd_req_t *request, const Die &die, uint64_t now_ms) {
  char buffer[512];
  const bool offline =
      die.last_seen_ms == 0 || now_ms - die.last_seen_ms > kDieOfflineMs;
  const bool active = die.last_activity_ms != 0 &&
                      now_ms - die.last_activity_ms <= kDieActiveMs;
  std::snprintf(buffer, sizeof(buffer),
                "{\"id\":\"%08lx\",\"type\":\"%s\",\"name\":",
                static_cast<unsigned long>(die.pixel_id),
                pixels::DieTypeName(die.type));
  httpd_resp_sendstr_chunk(request, buffer);
  SendJsonString(request, die.name);
  std::snprintf(buffer, sizeof(buffer),
                ",\"roll\":%d,\"hasRoll\":%s,\"battery\":%u,"
                "\"batteryState\":\"%s\",\"charging\":%s,\"rssi\":%d,"
                "\"offline\":%s,\"active\":%s,\"state\":\"%s\","
                "\"hasConnectedInfo\":%s,"
                "\"firmwareVersion\":%u,\"firmwareTimestamp\":%lu,"
                "\"profileHash\":\"%08lx\",\"availableFlash\":%lu,"
                "\"mcuTemperature\":",
                die.last_roll, die.has_roll ? "true" : "false", die.battery,
                BatteryStateName(die),
                die.charging ? "true" : "false", die.rssi,
                offline ? "true" : "false", active ? "true" : "false",
                pixels::RollStateName(die.roll_state),
                die.has_connected_info ? "true" : "false",
                die.firmware_version,
                static_cast<unsigned long>(die.firmware_timestamp),
                static_cast<unsigned long>(die.profile_hash),
                static_cast<unsigned long>(die.available_flash));
  httpd_resp_sendstr_chunk(request, buffer);
  if (die.has_temperature) {
    std::snprintf(buffer, sizeof(buffer), "%.2f,\"batteryTemperature\":%.2f}",
                  die.mcu_temperature_centi_c / 100.0,
                  die.battery_temperature_centi_c / 100.0);
    httpd_resp_sendstr_chunk(request, buffer);
  } else {
    httpd_resp_sendstr_chunk(
        request, "null,\"batteryTemperature\":null}");
  }
}

void SendCalculator(httpd_req_t *request,
                    const CalculatorSnapshot &calculator) {
  char buffer[512];
  std::snprintf(
      buffer, sizeof(buffer),
        "{\"preset\":\"%s\",\"status\":\"%s\",\"modifier\":%d,\"dc\":%u,"
        "\"dcEnabled\":%s,\"threshold\":%u,\"poolCount\":%u,\"keepCount\":%u,"
        "\"poolSides\":%u,\"poolDie\":\"%s\","
        "\"critical\":%s,\"completedRolls\":%u,\"totalRolls\":%u,"
        "\"recipeStep\":%u,\"recipeSteps\":%u,\"expectedId\":\"%08lx\","
        "\"expectedType\":\"%s\",\"expectedName\":",
        CalculatorPresetName(calculator.config.preset),
        CalculatorStatusName(calculator.status), calculator.config.modifier,
        calculator.config.dc, calculator.config.dc_enabled ? "true" : "false",
        calculator.config.threshold, calculator.config.pool_count,
        calculator.config.keep_count,
        calculator.config.pool_sides,
        CalculatorPoolDieName(calculator.config.pool_sides),
        calculator.config.critical ? "true" : "false",
        calculator.completed_rolls, calculator.total_rolls,
        calculator.recipe_step, calculator.recipe_steps,
        static_cast<unsigned long>(calculator.expected_pixel_id),
        pixels::DieTypeName(calculator.expected_type));
  httpd_resp_sendstr_chunk(request, buffer);
  SendJsonString(request, calculator.expected_name);
  std::snprintf(buffer, sizeof(buffer),
                  ",\"result\":%d,\"hasResult\":%s,\"hasCheck\":%s,"
                  "\"checkPassed\":%s,\"rejected\":%s,\"instruction\":",
                  calculator.result, calculator.has_result ? "true" : "false",
                  calculator.has_check ? "true" : "false",
                  calculator.check_passed ? "true" : "false",
                  calculator.last_attempt_rejected ? "true" : "false");
  httpd_resp_sendstr_chunk(request, buffer);
  SendJsonString(request, calculator.instruction);
  httpd_resp_sendstr_chunk(request, ",\"resultText\":");
  SendJsonString(request, calculator.result_text);
  httpd_resp_sendstr_chunk(request, "}");
}

esp_err_t IndexHandler(httpd_req_t *request) {
  httpd_resp_set_type(request, "text/html");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_send(request, kIndexHtml, HTTPD_RESP_USE_STRLEN);
}

esp_err_t StateHandler(httpd_req_t *request) {
  const uint64_t now_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000);
  const Snapshot snapshot = model->GetSnapshot(now_ms);
  httpd_resp_set_type(request, "application/json");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_sendstr_chunk(request, "{\"dice\":[");
  for (std::size_t i = 0; i < snapshot.dice_count; ++i) {
    if (i != 0) {
      httpd_resp_sendstr_chunk(request, ",");
    }
    SendDie(request, snapshot.dice[i], now_ms);
  }
  httpd_resp_sendstr_chunk(request, "],\"candidates\":[");
  for (std::size_t i = 0; i < snapshot.candidate_count; ++i) {
    if (i != 0) {
      httpd_resp_sendstr_chunk(request, ",");
    }
    SendDie(request, snapshot.candidates[i], now_ms);
  }
  const char *mode = snapshot.aggregate_mode == AggregateMode::kSum
                         ? "SUM"
                         : snapshot.aggregate_mode == AggregateMode::kHigh
                               ? "HIGH"
                               : "LOW";
  char buffer[256];
  std::snprintf(buffer, sizeof(buffer),
                "],\"aggregate\":{\"mode\":\"%s\",\"value\":%lld,"
                "\"rollCount\":%lu,\"hasValue\":%s},\"calculator\":",
                mode, static_cast<long long>(snapshot.aggregate_value),
                static_cast<unsigned long>(snapshot.aggregate_roll_count),
                snapshot.has_aggregate ? "true" : "false");
  httpd_resp_sendstr_chunk(request, buffer);
  SendCalculator(request, snapshot.calculator);
  std::snprintf(buffer, sizeof(buffer),
                ",\"statusLed\":{\"supported\":%s,\"mode\":\"%s\","
                "\"state\":\"%s\"}",
                StatusLedSupported() ? "true" : "false",
                StatusLedModeName(GetStatusLedMode()),
                StatusLedStateName(GetStatusLedState()));
  httpd_resp_sendstr_chunk(request, buffer);
  const BatteryStatus battery = GetBatteryStatus();
  std::snprintf(buffer, sizeof(buffer),
                ",\"battery\":{\"supported\":%s,\"valid\":%s,"
                "\"percent\":%d,\"millivolts\":%d,\"externalPower\":%s,"
                "\"low\":%s}",
                battery.supported ? "true" : "false",
                battery.valid ? "true" : "false", battery.percent,
                battery.millivolts, battery.external_power ? "true" : "false",
                battery.low ? "true" : "false");
  httpd_resp_sendstr_chunk(request, buffer);
  SendHubSettings(request);
  httpd_resp_sendstr_chunk(request, ",\"history\":[");
  for (std::size_t i = 0; i < snapshot.history_count; ++i) {
    const RollEvent &event = snapshot.history[i];
    if (i != 0) {
      httpd_resp_sendstr_chunk(request, ",");
    }
    std::snprintf(buffer, sizeof(buffer),
                  "{\"id\":\"%08lx\",\"type\":\"%s\",\"name\":",
                  static_cast<unsigned long>(event.pixel_id),
                  pixels::DieTypeName(event.type));
    httpd_resp_sendstr_chunk(request, buffer);
    SendJsonString(request, event.name);
    std::snprintf(buffer, sizeof(buffer),
                  ",\"value\":%d,\"ageMs\":%llu}", event.value,
                  static_cast<unsigned long long>(now_ms -
                                                  event.timestamp_ms));
    httpd_resp_sendstr_chunk(request, buffer);
  }
  httpd_resp_sendstr_chunk(request, "]}");
  return httpd_resp_send_chunk(request, nullptr, 0);
}

void SseTask(void *context) {
  auto *request = static_cast<httpd_req_t *>(context);
  uint32_t last_revision = 0;
  uint64_t last_send_ms = 0;
  while (!sse_shutdown.load()) {
    const uint64_t now_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000);
    // Fold hub settings in so device-side theme/sleep changes push promptly.
    const uint32_t revision = model->Revision() + ThemeRevision() * 7919U +
                              IdleSleepMinutes() * 104729U;
    if (revision != last_revision || now_ms - last_send_ms >= 5000) {
      char event[64];
      std::snprintf(event, sizeof(event),
                    "event: state\ndata: %lu\n\n",
                    static_cast<unsigned long>(revision));
      if (httpd_resp_send_chunk(request, event, HTTPD_RESP_USE_STRLEN) !=
          ESP_OK) {
        break;
      }
      last_revision = revision;
      last_send_ms = now_ms;
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  httpd_req_async_handler_complete(request);
  sse_socket.store(-1);
  sse_active.store(false);
  xSemaphoreGive(sse_stopped);
  vTaskDelete(nullptr);
}

esp_err_t EventsHandler(httpd_req_t *request) {
  bool expected = false;
  if (!sse_active.compare_exchange_strong(expected, true)) {
    httpd_resp_set_status(request, "503 Service Unavailable");
    return httpd_resp_sendstr(request, "An event stream is already active");
  }

  httpd_req_t *async_request = nullptr;
  const esp_err_t begin_result =
      httpd_req_async_handler_begin(request, &async_request);
  if (begin_result != ESP_OK) {
    sse_active.store(false);
    return begin_result;
  }
  xSemaphoreTake(sse_stopped, 0);
  sse_socket.store(httpd_req_to_sockfd(async_request));
  httpd_resp_set_type(async_request, "text/event-stream");
  httpd_resp_set_hdr(async_request, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(async_request, "Connection", "keep-alive");
  if (xTaskCreate(SseTask, "pixels_sse", 4096, async_request, 2, nullptr) !=
      pdPASS) {
    sse_socket.store(-1);
    httpd_req_async_handler_complete(async_request);
    sse_active.store(false);
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

bool QueryValue(httpd_req_t *request, const char *key, char *value,
                std::size_t value_size) {
  const std::size_t length = httpd_req_get_url_query_len(request);
  if (length == 0 || length >= 160) {
    return false;
  }
  char query[160];
  if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK) {
    return false;
  }
  return httpd_query_key_value(query, key, value, value_size) == ESP_OK;
}

esp_err_t HistoryHandler(httpd_req_t *request) {
  char id_text[16];
  if (!QueryValue(request, "id", id_text, sizeof(id_text))) {
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                               "Missing Pixel ID");
  }
  char *end = nullptr;
  const uint32_t pixel_id =
      static_cast<uint32_t>(std::strtoul(id_text, &end, 16));
  if (end == id_text || *end != '\0' || pixel_id == 0) {
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                               "Invalid Pixel ID");
  }

  const uint64_t now_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000);
  const RollHistory history = model->GetRollHistory(pixel_id);
  httpd_resp_set_type(request, "application/json");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  char buffer[128];
  std::snprintf(buffer, sizeof(buffer), "{\"id\":\"%08lx\",\"history\":[",
                static_cast<unsigned long>(pixel_id));
  httpd_resp_sendstr_chunk(request, buffer);
  for (std::size_t i = 0; i < history.count; ++i) {
    if (i != 0) {
      httpd_resp_sendstr_chunk(request, ",");
    }
    const RollEvent &event = history.events[i];
    std::snprintf(buffer, sizeof(buffer),
                  "{\"value\":%d,\"ageMs\":%llu}", event.value,
                  static_cast<unsigned long long>(now_ms -
                                                  event.timestamp_ms));
    httpd_resp_sendstr_chunk(request, buffer);
  }
  httpd_resp_sendstr_chunk(request, "]}");
  return httpd_resp_send_chunk(request, nullptr, 0);
}

esp_err_t ActionHandler(httpd_req_t *request) {
  char operation[24];
  if (!QueryValue(request, "op", operation, sizeof(operation))) {
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                               "Missing operation");
  }
  bool success = false;
  bool save_change = true;
  if (std::strcmp(operation, "cycle") == 0) {
    model->CycleAggregate();
    success = true;
  } else if (std::strcmp(operation, "clear") == 0) {
    model->ClearAggregate();
    success = true;
  } else if (std::strcmp(operation, "ledMode") == 0) {
    success = CycleStatusLedMode();
    save_change = false;
  } else if (std::strcmp(operation, "theme") == 0 ||
             std::strcmp(operation, "sleep") == 0) {
    char value_text[8];
    char *end = nullptr;
    const long value =
        QueryValue(request, "value", value_text, sizeof(value_text))
            ? std::strtol(value_text, &end, 10)
            : -1;
    if (end == nullptr || *end != '\0' || value < 0 || value > 255) {
      return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                 "Invalid value");
    }
    save_change = false;
    if (operation[0] == 't') {
      if (static_cast<std::size_t>(value) >= kThemeCount) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "Unknown theme");
      }
      SetTheme(static_cast<std::size_t>(value));
      success = true;
    } else {
      success = SetIdleSleepMinutes(static_cast<uint8_t>(value));
      if (!success) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "Unsupported sleep timeout");
      }
    }
  } else if (std::strcmp(operation, "powerOff") == 0) {
    if (!IsPowerOffSupported()) {
      return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                 "Power off is not supported on this board");
    }
    RequestPowerOff("Powering off");
    success = true;
    save_change = false;
  } else if (std::strcmp(operation, "factoryReset") == 0) {
    // Answer first so the browser sees the acknowledgement, then reset from a
    // short-lived task rather than inside the HTTP handler.
    httpd_resp_sendstr(request, "ok");
    xTaskCreate([](void *) { FactoryReset(); }, "factory_reset", 3072,
                nullptr, 5, nullptr);
    return ESP_OK;
  } else if (std::strcmp(operation, "calcStart") == 0) {
    if (!model->StartCalculator()) {
      httpd_resp_set_status(request, "409 Conflict");
      return httpd_resp_sendstr(request,
                                "Calculator is already rolling or cannot start");
    }
    success = true;
    save_change = false;
  } else if (std::strcmp(operation, "calcCancel") == 0) {
    model->CancelCalculator();
    success = true;
    save_change = false;
  } else if (std::strncmp(operation, "calc", 4) == 0) {
    CalculatorSetting setting = CalculatorSetting::kPreset;
    int delta = 1;
    if (std::strcmp(operation, "calcPrev") == 0) {
      delta = -1;
    } else if (std::strcmp(operation, "calcNext") == 0) {
      delta = 1;
    } else if (std::strcmp(operation, "calcModDown") == 0) {
      setting = CalculatorSetting::kModifier;
      delta = -1;
    } else if (std::strcmp(operation, "calcModUp") == 0) {
      setting = CalculatorSetting::kModifier;
    } else if (std::strcmp(operation, "calcDcToggle") == 0) {
      setting = CalculatorSetting::kDcEnabled;
    } else if (std::strcmp(operation, "calcDcDown") == 0) {
      setting = CalculatorSetting::kDc;
      delta = -1;
    } else if (std::strcmp(operation, "calcDcUp") == 0) {
      setting = CalculatorSetting::kDc;
    } else if (std::strcmp(operation, "calcThresholdDown") == 0) {
      setting = CalculatorSetting::kThreshold;
      delta = -1;
    } else if (std::strcmp(operation, "calcThresholdUp") == 0) {
      setting = CalculatorSetting::kThreshold;
    } else if (std::strcmp(operation, "calcPoolDown") == 0) {
      setting = CalculatorSetting::kPoolCount;
      delta = -1;
    } else if (std::strcmp(operation, "calcPoolUp") == 0) {
      setting = CalculatorSetting::kPoolCount;
    } else if (std::strcmp(operation, "calcKeepDown") == 0) {
      setting = CalculatorSetting::kKeepCount;
      delta = -1;
    } else if (std::strcmp(operation, "calcKeepUp") == 0) {
      setting = CalculatorSetting::kKeepCount;
    } else if (std::strcmp(operation, "calcDie") == 0) {
      setting = CalculatorSetting::kPoolSides;
    } else if (std::strcmp(operation, "calcCrit") == 0) {
      setting = CalculatorSetting::kCritical;
    } else {
      return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                 "Unknown calculator operation");
    }
    model->AdjustCalculator(setting, delta);
    success = true;
    save_change = false;
  } else {
    char id_text[16];
    if (!QueryValue(request, "id", id_text, sizeof(id_text))) {
      return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                 "Missing Pixel ID");
    }
    char *end = nullptr;
    const uint32_t pixel_id =
        static_cast<uint32_t>(std::strtoul(id_text, &end, 16));
    if (end == id_text || *end != '\0' || pixel_id == 0) {
      return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                 "Invalid Pixel ID");
    }
    if (std::strcmp(operation, "blink") == 0) {
      const esp_err_t result = RequestPixelsBlink(pixel_id);
      if (result != ESP_OK) {
        if (result == ESP_ERR_NO_MEM) {
          httpd_resp_set_status(request, "503 Service Unavailable");
          return httpd_resp_sendstr(request, "Die command queue is full");
        }
        return httpd_resp_send_err(request, HTTPD_404_NOT_FOUND,
                                   "Die is not currently available");
      }
      success = true;
      save_change = false;
    } else if (std::strcmp(operation, "info") == 0) {
      const esp_err_t result = RequestPixelsInfo(pixel_id);
      if (result != ESP_OK) {
        if (result == ESP_ERR_NO_MEM) {
          httpd_resp_set_status(request, "503 Service Unavailable");
          return httpd_resp_sendstr(request, "Die command queue is full");
        }
        return httpd_resp_send_err(request, HTTPD_404_NOT_FOUND,
                                   "Die is not currently available");
      }
      success = true;
      save_change = false;
    } else if (std::strcmp(operation, "pair") == 0) {
      success = model->Pair(pixel_id);
      if (success) {
        RequestPixelsBlink(pixel_id);
        RequestPixelsInfo(pixel_id);
      }
    } else if (std::strcmp(operation, "unpair") == 0) {
      success = model->Unpair(pixel_id);
    } else {
      return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                 "Unknown operation");
    }
  }
  if (!success || (save_change && !SavePreferences(*model))) {
    return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                               "Unable to save change");
  }
  return httpd_resp_sendstr(request, "OK");
}

void DecodeFormValue(char *value) {
  char *source = value;
  char *destination = value;
  while (*source != '\0') {
    if (*source == '+') {
      *destination++ = ' ';
      ++source;
    } else if (*source == '%' && std::isxdigit(source[1]) &&
               std::isxdigit(source[2])) {
      char hex[3] = {source[1], source[2], '\0'};
      *destination++ = static_cast<char>(std::strtoul(hex, nullptr, 16));
      source += 3;
    } else {
      *destination++ = *source++;
    }
  }
  *destination = '\0';
}

bool FormValue(char *body, const char *key, char *value,
               std::size_t value_size) {
  const std::size_t key_length = std::strlen(key);
  for (char *field = body; field != nullptr;) {
    char *next = std::strchr(field, '&');
    if (next != nullptr) {
      *next++ = '\0';
    }
    if (std::strncmp(field, key, key_length) == 0 &&
        field[key_length] == '=') {
      std::strncpy(value, field + key_length + 1, value_size - 1);
      value[value_size - 1] = '\0';
      DecodeFormValue(value);
      return true;
    }
    field = next;
  }
  return false;
}

esp_err_t SaveWifiCredentials(const char *ssid, const char *password) {
  nvs_handle_t handle;
  esp_err_t result = nvs_open("wifi_config", NVS_READWRITE, &handle);
  if (result != ESP_OK) {
    return result;
  }
  result = nvs_set_str(handle, "ssid", ssid);
  if (result == ESP_OK) {
    result = nvs_set_str(handle, "password", password);
  }
  if (result == ESP_OK) {
    result = nvs_commit(handle);
  }
  nvs_close(handle);
  return result;
}

esp_err_t SaveWifiEnabled(bool enabled) {
  nvs_handle_t handle;
  esp_err_t result = nvs_open("wifi_config", NVS_READWRITE, &handle);
  if (result != ESP_OK) {
    return result;
  }
  result = nvs_set_u8(handle, "enabled", enabled ? 1 : 0);
  if (result == ESP_OK) {
    result = nvs_commit(handle);
  }
  nvs_close(handle);
  return result;
}

esp_err_t ApplyStationCredentials(const char *ssid, const char *password) {
  if (ssid[0] == '\0') {
    station_enabled.store(false);
    network_status.store(NetworkStatus::kAccessPoint);
    esp_wifi_disconnect();
    wifi_config_t station = {};
    return esp_wifi_set_config(WIFI_IF_STA, &station);
  }
  const std::size_t password_length = std::strlen(password);
  if (password_length > 0 &&
      (password_length < 8 || password_length > 63)) {
    return ESP_ERR_INVALID_ARG;
  }
  wifi_config_t station = {};
  std::strncpy(reinterpret_cast<char *>(station.sta.ssid), ssid,
               sizeof(station.sta.ssid));
  std::strncpy(reinterpret_cast<char *>(station.sta.password), password,
               sizeof(station.sta.password));
  station.sta.threshold.authmode =
      password[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
  ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &station), kTag,
                      "Unable to set station configuration");
  station_enabled.store(true);
  network_status.store(NetworkStatus::kConnecting);
  return esp_wifi_connect();
}

esp_err_t WifiHandler(httpd_req_t *request) {
  if (request->content_len <= 0 || request->content_len >= 200) {
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                               "Invalid form data");
  }
  char body[200] = {};
  int received = 0;
  while (received < request->content_len) {
    const int result = httpd_req_recv(request, body + received,
                                      request->content_len - received);
    if (result <= 0) {
      return ESP_FAIL;
    }
    received += result;
  }
  char ssid[33] = {};
  char password[65] = {};
  FormValue(body, "password", password, sizeof(password));
  if (!FormValue(body, "ssid", ssid, sizeof(ssid))) {
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                               "Missing network name");
  }
  const esp_err_t apply_result = ApplyStationCredentials(ssid, password);
  if (apply_result != ESP_OK) {
    return httpd_resp_send_err(
        request, HTTPD_400_BAD_REQUEST,
        "Wi-Fi password must be empty or between 8 and 63 characters");
  }
  if (SaveWifiCredentials(ssid, password) != ESP_OK) {
    return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                               "Unable to save Wi-Fi credentials");
  }
  return httpd_resp_sendstr(
      request, ssid[0] == '\0'
                   ? "Home Wi-Fi disabled; access point remains active."
                   : "Saved. The device is connecting while the access point "
                     "remains active.");
}

void WifiEvent(void *, esp_event_base_t event_base, int32_t event_id,
               void *event_data) {
  if (event_base == WIFI_EVENT &&
      event_id == WIFI_EVENT_STA_DISCONNECTED && station_enabled.load()) {
    network_status.store(NetworkStatus::kConnecting);
    station_address.store(0);
    esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP &&
             wifi_started.load() && station_enabled.load()) {
    const auto *event = static_cast<const ip_event_got_ip_t *>(event_data);
    station_address.store(event->ip_info.ip.addr);
    network_status.store(NetworkStatus::kConnected);
    ESP_LOGI(kTag, "Connected to home Wi-Fi at " IPSTR,
             IP2STR(&event->ip_info.ip));
  }
}

esp_err_t StartWifi() {
  if (!wifi_initialized) {
    ESP_RETURN_ON_ERROR(esp_netif_init(), kTag, "Unable to initialize TCP/IP");
    const esp_err_t event_result = esp_event_loop_create_default();
    if (event_result != ESP_OK && event_result != ESP_ERR_INVALID_STATE) {
      return event_result;
    }
    esp_netif_t *access_point_netif = esp_netif_create_default_wifi_ap();
    esp_netif_t *station_netif = esp_netif_create_default_wifi_sta();
    if (access_point_netif != nullptr) {
      // Use an uncommon subnet so the hotspot does not collide with typical
      // home/VLAN ranges (192.168.x.x, 10.0.x.x) and get routed away.
      esp_netif_ip_info_t ip_info = {};
      ip_info.ip.addr = esp_ip4addr_aton(kAccessPointAddress);
      ip_info.gw.addr = ip_info.ip.addr;
      ip_info.netmask.addr = esp_ip4addr_aton("255.255.255.0");
      esp_netif_dhcps_stop(access_point_netif);
      esp_netif_set_ip_info(access_point_netif, &ip_info);
      esp_netif_dhcps_start(access_point_netif);
    }

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    bool driver_initialized = false;
    bool wifi_handler_registered = false;
    bool ip_handler_registered = false;
    const auto cleanup = [&]() {
      if (ip_handler_registered) {
        esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, WifiEvent);
      }
      if (wifi_handler_registered) {
        esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, WifiEvent);
      }
      if (driver_initialized) {
        esp_wifi_deinit();
      }
      if (station_netif != nullptr) {
        esp_netif_destroy_default_wifi(station_netif);
      }
      if (access_point_netif != nullptr) {
        esp_netif_destroy_default_wifi(access_point_netif);
      }
    };
    esp_err_t result = esp_wifi_init(&init);
    if (result != ESP_OK) {
      cleanup();
      return result;
    }
    driver_initialized = true;
    result = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        WifiEvent, nullptr);
    if (result != ESP_OK) {
      cleanup();
      return result;
    }
    wifi_handler_registered = true;
    result = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        WifiEvent, nullptr);
    if (result != ESP_OK) {
      cleanup();
      return result;
    }
    ip_handler_registered = true;

    wifi_config_t access_point = {};
    std::strncpy(reinterpret_cast<char *>(access_point.ap.ssid),
                 kAccessPointSsid, sizeof(access_point.ap.ssid));
    std::strncpy(reinterpret_cast<char *>(access_point.ap.password),
                 kAccessPointPassword, sizeof(access_point.ap.password));
    access_point.ap.ssid_len = std::strlen(kAccessPointSsid);
    access_point.ap.channel = 1;
    access_point.ap.max_connection = 4;
    access_point.ap.authmode = WIFI_AUTH_WPA2_PSK;

    result = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (result != ESP_OK) {
      cleanup();
      return result;
    }
    result = esp_wifi_set_config(WIFI_IF_AP, &access_point);
    if (result != ESP_OK) {
      cleanup();
      return result;
    }
    wifi_initialized = true;
  }
  ESP_RETURN_ON_ERROR(esp_wifi_start(), kTag, "Unable to start Wi-Fi");
  wifi_started.store(true);
  network_status.store(NetworkStatus::kAccessPoint);

  char ssid[33] = {};
  char password[65] = {};
  nvs_handle_t handle;
  if (nvs_open("wifi_config", NVS_READONLY, &handle) == ESP_OK) {
    std::size_t ssid_size = sizeof(ssid);
    std::size_t password_size = sizeof(password);
    nvs_get_str(handle, "ssid", ssid, &ssid_size);
    nvs_get_str(handle, "password", password, &password_size);
    nvs_close(handle);
  }
  if (ssid[0] != '\0') {
    const esp_err_t result = ApplyStationCredentials(ssid, password);
    if (result != ESP_OK) {
      station_enabled.store(false);
      network_status.store(NetworkStatus::kAccessPoint);
      ESP_LOGW(kTag,
               "Ignoring invalid saved Wi-Fi configuration; access point "
               "remains available: %s",
               esp_err_to_name(result));
    }
  }
  ESP_LOGI(kTag, "Web UI available on Wi-Fi '%s' at http://%s",
           kAccessPointSsid, kAccessPointAddress);
  return ESP_OK;
}

} // namespace

esp_err_t StartWebServer(DiceModel *dice_model) {
  if (dice_model == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  model = dice_model;
  sse_shutdown.store(false);
  if (sse_stopped == nullptr) {
    sse_stopped = xSemaphoreCreateBinary();
    if (sse_stopped == nullptr) {
      return ESP_ERR_NO_MEM;
    }
  }
  if (server != nullptr) {
    return ESP_OK;
  }
  ESP_RETURN_ON_ERROR(StartWifi(), kTag, "Unable to start network");

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 8;
  config.stack_size = 8192;
  esp_err_t result = httpd_start(&server, &config);
  if (result != ESP_OK) {
    StopWebServer();
    return result;
  }

  const httpd_uri_t index = {
      .uri = "/",
      .method = HTTP_GET,
      .handler = IndexHandler,
      .user_ctx = nullptr,
  };
  const httpd_uri_t state = {
      .uri = "/api/state",
      .method = HTTP_GET,
      .handler = StateHandler,
      .user_ctx = nullptr,
  };
  const httpd_uri_t action = {
      .uri = "/api/action",
      .method = HTTP_POST,
      .handler = ActionHandler,
      .user_ctx = nullptr,
  };
  const httpd_uri_t events = {
      .uri = "/api/events",
      .method = HTTP_GET,
      .handler = EventsHandler,
      .user_ctx = nullptr,
  };
  const httpd_uri_t history = {
      .uri = "/api/history",
      .method = HTTP_GET,
      .handler = HistoryHandler,
      .user_ctx = nullptr,
  };
  const httpd_uri_t wifi = {
      .uri = "/api/wifi",
      .method = HTTP_POST,
      .handler = WifiHandler,
      .user_ctx = nullptr,
  };
  result = httpd_register_uri_handler(server, &index);
  if (result == ESP_OK) {
    result = httpd_register_uri_handler(server, &state);
  }
  if (result == ESP_OK) {
    result = httpd_register_uri_handler(server, &action);
  }
  if (result == ESP_OK) {
    result = httpd_register_uri_handler(server, &events);
  }
  if (result == ESP_OK) {
    result = httpd_register_uri_handler(server, &history);
  }
  if (result == ESP_OK) {
    result = httpd_register_uri_handler(server, &wifi);
  }
  if (result != ESP_OK) {
    StopWebServer();
  }
  return result;
}

esp_err_t StopWebServer() {
  station_enabled.store(false);
  wifi_started.store(false);
  network_status.store(NetworkStatus::kOff);
  sse_shutdown.store(true);
  if (sse_active.load()) {
    const int socket = sse_socket.load();
    if (server != nullptr && socket >= 0) {
      httpd_sess_trigger_close(server, socket);
    }
    if (xSemaphoreTake(sse_stopped, pdMS_TO_TICKS(5000)) != pdTRUE) {
      ESP_LOGE(kTag, "Timed out stopping the SSE worker");
      return ESP_ERR_TIMEOUT;
    }
  }
  esp_err_t result = ESP_OK;
  if (server != nullptr) {
    result = httpd_stop(server);
    server = nullptr;
  }
  if (wifi_initialized) {
    const esp_err_t stop_result = esp_wifi_stop();
    if (result == ESP_OK && stop_result != ESP_ERR_WIFI_NOT_STARTED) {
      result = stop_result;
    }
  }
  return result;
}

esp_err_t SetWebNetworkEnabled(DiceModel *dice_model, bool enabled) {
  const bool was_enabled = IsWebNetworkEnabled();
  if (was_enabled == enabled) {
    return SaveWifiEnabled(enabled);
  }
  const esp_err_t result =
      enabled ? StartWebServer(dice_model) : StopWebServer();
  if (result != ESP_OK) {
    return result;
  }
  const esp_err_t save_result = SaveWifiEnabled(enabled);
  if (save_result == ESP_OK) {
    return ESP_OK;
  }
  const esp_err_t rollback_result =
      was_enabled ? StartWebServer(dice_model) : StopWebServer();
  if (rollback_result != ESP_OK) {
    ESP_LOGE(kTag, "Unable to roll back Wi-Fi after persistence failure: %s",
             esp_err_to_name(rollback_result));
  }
  return save_result;
}

bool RestoreWebNetworkEnabled(bool default_value) {
  nvs_handle_t handle;
  if (nvs_open("wifi_config", NVS_READONLY, &handle) != ESP_OK) {
    return default_value;
  }
  uint8_t enabled = default_value ? 1 : 0;
  nvs_get_u8(handle, "enabled", &enabled);
  nvs_close(handle);
  return enabled != 0;
}

bool IsWebNetworkEnabled() {
  return network_status.load() != NetworkStatus::kOff;
}

NetworkStatus GetWebNetworkStatus() { return network_status.load(); }

const char *GetWebAddress(char *buffer, std::size_t size) {
  const uint32_t address = station_address.load();
  if (network_status.load() == NetworkStatus::kConnected && address != 0) {
    esp_ip4_addr_t ip = {address};
    std::snprintf(buffer, size, IPSTR, IP2STR(&ip));
  } else {
    std::snprintf(buffer, size, "%s", kAccessPointAddress);
  }
  return buffer;
}

const char *NetworkStatusName(NetworkStatus status) {
  switch (status) {
  case NetworkStatus::kAccessPoint:
    return "AP";
  case NetworkStatus::kConnecting:
    return "CONNECTING";
  case NetworkStatus::kConnected:
    return "CONNECTED";
  case NetworkStatus::kOff:
  default:
    return "OFF";
  }
}

} // namespace app
