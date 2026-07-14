#include "portal_page.h"

const char PAGE[] = R"HTML(

<!DOCTYPE html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
<title>RAK3112 LoRaWAN</title>
<style>
:root{
  --bg:#F3F5F8;
  --surface:#FFFFFF;
  --surface-alt:#F8FAFC;
  --border:#DEE3EC;
  --text:#131722;
  --muted:#62697B;
  --accent:#2B6CEE;
  --accent-ink:#FFFFFF;
  --accent-2:#0EA5A0;
  --warn:#B0650A;
  --warn-bg:#FCEFD9;
  --danger:#C6392F;
  --danger-bg:#FBE4E1;
  --success:#188752;
  --success-bg:#DEF3E7;
  --shadow:0 1px 2px rgba(20,25,40,.04), 0 8px 24px -12px rgba(20,25,40,.10);
  --radius:12px;
  --mono:'Times New Roman',Times,serif;
  --sans:'Times New Roman',Times,serif;
  color-scheme:light;
}
:root[data-theme="dark"]{
  --bg:#090C11;
  --surface:#11151D;
  --surface-alt:#161B25;
  --border:#232A38;
  --text:#E8ECF5;
  --muted:#8992A6;
  --accent:#5B9BFF;
  --accent-ink:#06101F;
  --accent-2:#2DD9CE;
  --warn:#F5A524;
  --warn-bg:#2E2410;
  --danger:#FF6259;
  --danger-bg:#301315;
  --success:#34D399;
  --success-bg:#0F2A22;
  --shadow:0 1px 2px rgba(0,0,0,.3), 0 10px 30px -14px rgba(0,0,0,.6);
  color-scheme:dark;
}
*{box-sizing:border-box;margin:0;padding:0;}
html{-webkit-tap-highlight-color:transparent;}
body{
  font-family:var(--sans);
  background:
    radial-gradient(1200px 480px at 50% -180px, color-mix(in srgb, var(--accent) 7%, transparent), transparent 60%),
    var(--bg);
  color:var(--text);
  font-size:14px;
  min-height:100vh;
  padding-bottom:28px;
  transition:background-color .25s ease,color .25s ease;
}
@media (prefers-reduced-motion:reduce){*{animation-duration:.001ms !important;transition-duration:.001ms !important;}}

/* ── Login ── */
#loginPage{min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px;}
.login-card{position:relative;overflow:hidden;background:var(--surface);border:1px solid var(--border);border-radius:16px;padding:32px 24px;width:100%;max-width:340px;box-shadow:var(--shadow);}
.login-mark{width:44px;height:44px;border-radius:11px;background:linear-gradient(155deg,var(--accent),var(--accent-2));display:flex;align-items:center;justify-content:center;margin-bottom:18px;}
.login-mark svg{width:24px;height:24px;stroke:var(--accent-ink);}
.login-card h2{font-size:19px;font-weight:700;color:var(--text);margin-bottom:4px;letter-spacing:-.01em;}
.login-card p{font-size:13px;color:var(--muted);margin-bottom:22px;}
.login-card input{width:100%;padding:13px 14px;border:1px solid var(--border);border-radius:9px;font-size:16px;color:var(--text);background:var(--surface-alt);outline:none;font-family:var(--mono);letter-spacing:.04em;transition:border-color .15s;}
.login-card input:focus{border-color:var(--accent);}
.login-card .err{color:var(--danger);font-size:12px;margin-top:8px;min-height:16px;font-weight:500;}
.login-card button{width:100%;margin-top:16px;padding:13px;background:var(--accent);color:var(--accent-ink);border:none;border-radius:9px;font-size:15px;font-weight:700;cursor:pointer;transition:filter .15s,transform .08s;}
.login-card button:hover{filter:brightness(1.08);}
.login-card button:active{transform:scale(.98);}
.login-foot{margin-top:18px;font-size:11px;color:var(--muted);text-align:center;font-family:var(--mono);}

/* ── Layout ── */
header{
  position:sticky;top:0;z-index:20;
  background:color-mix(in srgb, var(--surface) 88%, transparent);
  backdrop-filter:blur(10px);-webkit-backdrop-filter:blur(10px);
  border-bottom:1px solid var(--border);
  padding:12px 16px;
  display:flex;align-items:center;justify-content:space-between;gap:10px;flex-wrap:wrap;
}
.brand{display:flex;align-items:center;gap:10px;min-width:0;}
.brand-mark{flex:0 0 auto;width:32px;height:32px;border-radius:9px;background:linear-gradient(155deg,var(--accent),var(--accent-2));display:flex;align-items:center;justify-content:center;}
.brand-mark svg{width:18px;height:18px;stroke:var(--accent-ink);}
.brand-text{min-width:0;}
.brand-text h1{font-size:14px;font-weight:700;color:var(--text);letter-spacing:-.01em;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;}
.brand-text .sub{font-size:10.5px;color:var(--muted);font-family:var(--mono);letter-spacing:.03em;}

.header-actions{display:flex;align-items:center;gap:8px;}

.status-pill{display:inline-flex;align-items:center;gap:7px;padding:6px 12px;border-radius:20px;font-size:12px;font-weight:600;border:1px solid transparent;}
.pill-ok{background:var(--success-bg);color:var(--success);}
.pill-err{background:var(--danger-bg);color:var(--danger);}
.pill-idle{background:var(--surface-alt);color:var(--muted);border-color:var(--border);}
.dot{width:7px;height:7px;border-radius:50%;background:currentColor;position:relative;}
.pill-ok .dot::after{content:'';position:absolute;inset:-4px;border-radius:50%;background:currentColor;opacity:.35;animation:pulse 1.8s ease-out infinite;}
@keyframes pulse{0%{transform:scale(.6);opacity:.45;}100%{transform:scale(2.2);opacity:0;}}

.theme-toggle{display:flex;align-items:center;justify-content:center;width:34px;height:34px;border-radius:9px;border:1px solid var(--border);background:var(--surface-alt);color:var(--text);cursor:pointer;transition:background .15s,transform .08s;}
.theme-toggle:hover{background:var(--border);}
.theme-toggle:active{transform:scale(.94);}
.theme-toggle svg{width:17px;height:17px;stroke:currentColor;}

.page{padding:16px;max-width:1320px;width:98%;margin:0 auto;display:grid;grid-template-columns:1fr;gap:16px;}
@media (min-width:860px){
  .page{grid-template-columns:1.15fr 1fr;align-items:start;}
  .span-2{grid-column:1 / -1;}
}

.card{background:var(--surface);border:1px solid var(--border);border-radius:var(--radius);padding:16px;box-shadow:var(--shadow);}
.card h2{font-size:11.5px;font-weight:700;color:var(--muted);text-transform:uppercase;letter-spacing:.08em;margin-bottom:14px;display:flex;align-items:center;gap:7px;}
.card h2 .ic{width:14px;height:14px;stroke:var(--muted);flex:0 0 auto;}

/* ── Form ── */
.formgrid{display:flex;flex-direction:column;gap:13px;}
label{display:block;font-size:11.5px;font-weight:600;color:var(--muted);margin-bottom:5px;letter-spacing:.02em;}
input,select{width:100%;padding:11px 12px;border:1px solid var(--border);border-radius:8px;font-size:15px;color:var(--text);background:var(--surface-alt);outline:none;transition:border-color .15s,box-shadow .15s;font-family:var(--sans);}
input[id$="EUI"],input[id$="Key"],input[id$="SKey"],input[id="devAddr"]{font-family:var(--mono);font-size:13px;letter-spacing:.02em;}
input:focus,select:focus{border-color:var(--accent);box-shadow:0 0 0 3px color-mix(in srgb, var(--accent) 18%, transparent);}
input::placeholder{color:var(--muted);opacity:.7;}

.check-row{display:flex;align-items:center;gap:10px;padding:8px 0;cursor:pointer;}
.check-row input[type=checkbox]{width:19px;height:19px;accent-color:var(--accent);cursor:pointer;}
.check-row span{font-size:13.5px;font-weight:500;color:var(--text);}

.section-title{font-size:11.5px;font-weight:600;color:var(--muted);letter-spacing:.02em;margin-bottom:8px;}
.interval-grid{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;}
@media (max-width:420px){.interval-grid{grid-template-columns:repeat(2,1fr);}}
.interval-grid .lbl{font-size:10.5px;color:var(--muted);margin-bottom:4px;font-weight:600;text-transform:uppercase;letter-spacing:.04em;}
.interval-grid input{text-align:center;padding:9px 6px;}

hr.sep{border:none;border-top:1px solid var(--border);margin:2px 0;}

/* ── Buttons ── */
.btn-row{display:flex;gap:8px;flex-wrap:wrap;margin-top:6px;}
.btn{padding:12px 14px;border-radius:8px;border:1px solid transparent;cursor:pointer;font-size:13.5px;font-weight:700;flex:1;min-width:96px;transition:filter .15s,transform .08s,box-shadow .15s;letter-spacing:.01em;}
.btn-save{background:var(--accent);color:var(--accent-ink);}
.btn-join{background:var(--success);color:#08281A;}
.btn-leave{background:var(--surface-alt);color:var(--text);border-color:var(--border);}
.btn-force{background:var(--warn);color:#2B1900;}
.btn:hover{filter:brightness(1.06);}
.btn:active{transform:scale(.97);}
.btn:focus-visible{outline:2px solid var(--accent);outline-offset:2px;}

/* ── Log ── */
#log{font-family:var(--mono);font-size:11px;background:var(--surface-alt);border:1px solid var(--border);border-radius:9px;padding:11px;height:230px;overflow-y:auto;line-height:1.55;color:var(--text);word-break:break-all;}
#log div{padding:1px 0;}
.log-up{color:var(--accent);}
.log-down{color:var(--accent-2);}
.log-err{color:var(--danger);}

/* ── Downlink state monitor ── */
#dlStateBox{background:var(--surface-alt);border:1px solid var(--border);border-radius:9px;padding:4px 12px;}
.dl-row{padding:10px 0;border-bottom:1px solid var(--border);display:flex;flex-wrap:wrap;gap:6px;justify-content:space-between;align-items:center;}
.dl-row:last-child{border-bottom:none;}
.dl-label{color:var(--muted);font-size:12px;font-weight:600;}
.dl-value{color:var(--text);font-size:13px;font-weight:600;text-align:right;font-family:var(--mono);}
.dl-highlight{color:var(--accent-2);font-weight:700;}
.dl-warning{color:var(--warn);}

/* ── Downlink history ── */
#downlinkBox{background:var(--surface-alt);border:1px solid var(--border);border-radius:9px;padding:10px;min-height:70px;font-family:var(--mono);font-size:11px;line-height:1.7;color:var(--text);overflow-y:auto;max-height:180px;}
#downlinkBox .empty{color:var(--muted);}
.dl-entry{display:flex;flex-wrap:wrap;gap:8px;align-items:center;padding:5px 0;border-bottom:1px solid var(--border);}
.dl-entry:last-child{border-bottom:none;}
.dl-time{color:var(--muted);font-size:10.5px;}
.dl-port{background:color-mix(in srgb, var(--accent-2) 18%, transparent);color:var(--accent-2);border-radius:5px;padding:2px 7px;font-size:10px;font-weight:700;white-space:nowrap;}
.dl-hex{color:var(--accent);}
.dl-ascii{color:var(--muted);}

.card-head{display:flex;align-items:center;justify-content:space-between;gap:8px;margin-bottom:12px;}
.card-head h2{margin-bottom:0;}
.btn-clear{padding:7px 12px;border-radius:7px;border:1px solid var(--border);background:var(--surface-alt);color:var(--text);font-size:12px;font-weight:700;cursor:pointer;transition:background .15s;}
.btn-clear:hover{background:var(--border);}
.btn-clear.is-busy{opacity:.6;cursor:wait;}
.mini{padding:7px 11px;border-radius:7px;border:1px solid var(--border);background:var(--surface-alt);color:var(--text);font-size:12px;font-weight:700;cursor:pointer;transition:background .15s;}
.mini:hover{background:var(--border);}
.meta-grid{display:grid;grid-template-columns:repeat(2,1fr);gap:8px;margin-top:12px;}
@media (max-width:380px){.meta-grid{grid-template-columns:1fr;}}
.meta-item{background:var(--surface-alt);border:1px solid var(--border);border-radius:8px;padding:9px 10px;}
.meta-item .k{font-size:10.5px;color:var(--muted);font-weight:600;text-transform:uppercase;letter-spacing:.04em;margin-bottom:3px;}
.meta-item .v{font-size:12.5px;color:var(--text);font-weight:600;word-break:break-all;font-family:var(--mono);}
</style>
</head><body>

<!-- ═══ LOGIN ═══ -->
<div id="loginPage">
  <div class="login-card">
    <div class="login-mark"><svg viewBox="0 0 24 24" fill="none" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 20v-7"/><path d="M8.5 16.5a5 5 0 0 1 0-9"/><path d="M15.5 16.5a5 5 0 0 0 0-9"/><path d="M5.5 20a9 9 0 0 1 0-16"/><path d="M18.5 20a9 9 0 0 0 0-16"/><circle cx="12" cy="10" r="2.2"/></svg></div>
    <h2>RAK3112 Portal</h2>
    <p>Enter the device password to continue</p>
    <input id="pass" type="password" placeholder="Password" autocomplete="off" onkeydown="if(event.key==='Enter')doLogin()">
    <div class="err" id="err"></div>
    <button onclick="doLogin()">Sign in</button>
    <div class="login-foot">192.168.4.1 · local device network</div>
  </div>
</div>

<!-- ═══ MAIN ═══ -->
<div id="mainPage" style="display:none">

<header>
  <div class="brand">
    <div class="brand-mark"><svg viewBox="0 0 24 24" fill="none" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 20v-7"/><path d="M8.5 16.5a5 5 0 0 1 0-9"/><path d="M15.5 16.5a5 5 0 0 0 0-9"/><path d="M5.5 20a9 9 0 0 1 0-16"/><path d="M18.5 20a9 9 0 0 0 0-16"/><circle cx="12" cy="10" r="2.2"/></svg></div>
    <div class="brand-text">
      <h1>RAK3112 LoRaWAN</h1>
      <div class="sub">Class&nbsp;C&nbsp;· SX1262</div>
    </div>
  </div>
  <div class="header-actions">
    <span class="status-pill pill-idle" id="statusPill"><span class="dot"></span><span id="statusTxt">Idle</span></span>
    <button class="theme-toggle" id="themeToggle" onclick="toggleTheme()" title="Toggle theme" aria-label="Toggle dark mode">
      <svg id="themeIcon" viewBox="0 0 24 24" fill="none" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"></svg>
    </button>
  </div>
</header>

<div class="page">

  <div class="card span-2">
    <div class="card-head">
      <h2><svg class="ic" viewBox="0 0 24 24" fill="none" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="4" width="18" height="16" rx="2"/><path d="M7 8h10M7 12h10M7 16h6"/></svg>Device info</h2>
      <button class="mini" onclick="toggleDeviceInfo()" id="deviceInfoBtn">Show UID / MAC / IP</button>
    </div>
    <div id="deviceInfoPanel" style="display:none">
      <div class="formgrid">
        <div>
          <label>UID name (editable — also sets the AP SSID)</label>
          <input id="uidName" placeholder="Device UID name">
        </div>
        <div>
          <label>Device IP (editable on board)</label>
          <input id="deviceIp" placeholder="192.168.4.1">
        </div>
        <div>
          <label>LED board UID (editable)</label>
          <input id="ledUid" placeholder="08-44">
        </div>
        <div>
          <label>LED board MAC (editable)</label>
          <input id="ledMac" placeholder="DE:AD:BE:EF:FE:ED">
        </div>
        <div>
          <label>LED board IP (editable)</label>
          <input id="ledBoardIp" placeholder="13.22.0.213">
        </div>
        <div class="btn-row">
          <button class="btn btn-save" onclick="saveDeviceInfo()">Save device info</button>
        </div>
      </div>
      <div class="meta-grid">
        <div class="meta-item"><div class="k">UID</div><div class="v" id="uidVal">--</div></div>
        <div class="meta-item"><div class="k">MAC</div><div class="v" id="macVal">--</div></div>
        <div class="meta-item"><div class="k">IP</div><div class="v" id="ipVal">--</div></div>
        <div class="meta-item"><div class="k">Board Default IP</div><div class="v" id="boardDefaultIpVal">--</div></div>
        <div class="meta-item"><div class="k">LED UID</div><div class="v" id="ledUidVal">--</div></div>
        <div class="meta-item"><div class="k">LED MAC</div><div class="v" id="ledMacVal">--</div></div>
        <div class="meta-item"><div class="k">LED IP</div><div class="v" id="ledIpVal">--</div></div>
        <div class="meta-item"><div class="k">AP SSID</div><div class="v" id="ssidVal">--</div></div>
      </div>
    </div>
  </div>

  <!-- Config -->
  <div class="card">
    <h2><svg class="ic" viewBox="0 0 24 24" fill="none" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 20V10M18 20V4M6 20v-4"/></svg>Configuration</h2>
    <div class="formgrid">
      <div>
        <label>Activation mode</label>
        <select id="mode">
          <option value="OTAA">OTAA</option>
          <option value="ABP">ABP</option>
        </select>
      </div>

      <div id="f_joinEUI"><label>JoinEUI</label><input id="joinEUI" placeholder="0000000000000000"></div>
      <div id="f_devEUI"><label>DevEUI</label><input id="devEUI" placeholder="0000000000000000"></div>
      <div id="f_appKey"><label>AppKey</label><input id="appKey" placeholder="32 hex chars"></div>
      <div id="f_nwkKey"><label>NwkKey (optional)</label><input id="nwkKey" placeholder="Leave blank for 1.0.x"></div>

      <div id="f_devAddr" style="display:none"><label>DevAddr</label><input id="devAddr" placeholder="00000000"></div>
      <div id="f_nwkSKey" style="display:none"><label>NwkSKey</label><input id="nwkSKey"></div>
      <div id="f_appSKey" style="display:none"><label>AppSKey</label><input id="appSKey"></div>

      <div>
        <label>Region</label>
        <select id="region">
          <option>EU868</option><option>US915</option><option>AU915</option><option>AS923</option>
          <option>KR920</option><option>IN865</option><option>CN470</option>
        </select>
      </div>
      <div>
        <label>FPort</label>
        <input id="fPort" type="number" value="1">
      </div>
      <div>
        <label>TX power (dBm)</label>
        <input id="txPower" type="number" value="14">
      </div>

      <div class="check-row"><input type="checkbox" id="adr"><span>Adaptive data rate (ADR)</span></div>
      <div class="check-row"><input type="checkbox" id="confirmed"><span>Confirmed uplinks</span></div>

      <div>
        <label>Custom payload</label>
        <input id="customPayload" placeholder="hi and im Dhaanes">
      </div>

      <hr class="sep">

      <div>
        <div class="section-title">Health ping interval</div>
        <div class="interval-grid">
          <div><div class="lbl">Sec</div><input id="intervalSec" type="number" value="2"></div>
          <div><div class="lbl">Min</div><input id="intervalMin" type="number" value="0"></div>
          <div><div class="lbl">Hour</div><input id="intervalHour" type="number" value="0"></div>
          <div><div class="lbl">Day</div><input id="intervalDay" type="number" value="0"></div>
        </div>
      </div>

      <div class="btn-row">
        <button class="btn btn-save" onclick="save()">Save</button>
        <button class="btn btn-join" onclick="join()">Join</button>
        <button class="btn btn-leave" onclick="leave()">Leave</button>
      </div>
      <div class="btn-row">
        <button class="btn btn-force" onclick="forceUplink()">Force uplink</button>
      </div>
    </div>
  </div>

  <div style="display:flex;flex-direction:column;gap:16px;">
    <!-- DOWNLINK STATE MONITOR -->
    <div class="card">
      <h2><svg class="ic" viewBox="0 0 24 24" fill="none" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 11a8 8 0 0 1 16 0"/><path d="M7 14a4.5 4.5 0 0 1 10 0"/><circle cx="12" cy="17" r="1.4" fill="currentColor" stroke="none"/></svg>Downlink state monitor</h2>
      <div id="dlStateBox">
        <div class="dl-row"><span class="dl-label">Uplink</span><span class="dl-value" id="uplinkState">Not sent yet</span></div>
        <div class="dl-row"><span class="dl-label">Downlink window</span><span class="dl-value" id="dlWindowState">Closed</span></div>
        <div class="dl-row"><span class="dl-label">Next uplink in</span><span class="dl-value dl-highlight" id="nextUplinkTimer">--</span></div>
        <div class="dl-row"><span class="dl-label">Time since uplink</span><span class="dl-value" id="timeSinceUplink">--</span></div>
        <div class="dl-row"><span class="dl-label">Downlink status</span><span class="dl-value" id="dlStatus">Waiting for uplink</span></div>
      </div>
    </div>

    <!-- Downlink history -->
    <div class="card">
      <h2><svg class="ic" viewBox="0 0 24 24" fill="none" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 3v12m0 0-4-4m4 4 4-4"/><path d="M4 17v2a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-2"/></svg>Downlink history</h2>
      <div id="downlinkBox"><span class="empty">No downlink yet</span></div>
    </div>
  </div>

  <!-- Live log -->
  <div class="card span-2">
    <div class="card-head">
      <h2><svg class="ic" viewBox="0 0 24 24" fill="none" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 6h16M4 12h10M4 18h13"/></svg>Live log</h2>
      <button id="clearLogsBtn" class="btn-clear" onclick="clearLogs()">Clear logs</button>
    </div>
    <div id="log">Waiting for events…</div>
  </div>

</div>
</div>

<script>
/* ── Theme ── */
const sunPath='<circle cx="12" cy="12" r="4.2"/><path d="M12 2.5v2.4M12 19.1v2.4M4.9 4.9l1.7 1.7M17.4 17.4l1.7 1.7M2.5 12h2.4M19.1 12h2.4M4.9 19.1l1.7-1.7M17.4 6.6l1.7-1.7"/>';
const moonPath='<path d="M20.5 14.5a8.5 8.5 0 1 1-9-11 7 7 0 0 0 9 11Z"/>';
function applyTheme(t){
  document.documentElement.setAttribute('data-theme', t);
  document.getElementById('themeIcon').innerHTML = (t==='dark') ? sunPath : moonPath;
  try{ localStorage.setItem('rak3112-theme', t); }catch(e){}
}
function toggleTheme(){
  const cur = document.documentElement.getAttribute('data-theme') === 'dark' ? 'dark' : 'light';
  applyTheme(cur==='dark' ? 'light' : 'dark');
}
(function initTheme(){
  let saved=null;
  try{ saved = localStorage.getItem('rak3112-theme'); }catch(e){}
  if(!saved){
    saved = (window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches) ? 'dark' : 'light';
  }
  applyTheme(saved);
})();

function doLogin(){
  if(document.getElementById("pass").value==="2240624"){
    document.getElementById("loginPage").style.display="none";
    document.getElementById("mainPage").style.display="block";
    loadCfg();
    startPolling();
  }else{
    document.getElementById("err").textContent="Incorrect password.";
  }
}

function toggle(){
  const isOTAA=document.getElementById("mode").value==="OTAA";
  ["f_joinEUI","f_devEUI","f_appKey","f_nwkKey"].forEach(id=>{
    document.getElementById(id).style.display=isOTAA?"block":"none";
  });
  ["f_devAddr","f_nwkSKey","f_appSKey"].forEach(id=>{
    document.getElementById(id).style.display=isOTAA?"none":"block";
  });
}
document.getElementById("mode").addEventListener("change",toggle);

let deviceInfoLoaded=false;
async function loadDeviceInfo(){
  const r=await fetch('/api/deviceinfo');
  const j=await r.json();
  document.getElementById('uidName').value=j.uidName||'';
  document.getElementById('deviceIp').value=j.ip||'';
  document.getElementById('ledUid').value=j.ledUid||'';
  document.getElementById('ledMac').value=j.ledMac||'';
  document.getElementById('ledBoardIp').value=j.ledIp||'';
  document.getElementById('uidVal').textContent=j.uid||'--';
  document.getElementById('macVal').textContent=j.mac||'--';
  document.getElementById('ipVal').textContent=j.ip||'--';
  document.getElementById('boardDefaultIpVal').textContent=j.boardDefaultIp||'13.22.0.213';
  document.getElementById('ledUidVal').textContent=j.ledUid||'--';
  document.getElementById('ledMacVal').textContent=j.ledMac||'--';
  document.getElementById('ledIpVal').textContent=j.ledIp||'--';
  document.getElementById('ssidVal').textContent=j.ssid||'--';
}

async function toggleDeviceInfo(){
  const panel=document.getElementById('deviceInfoPanel');
  const btn=document.getElementById('deviceInfoBtn');
  const willShow=panel.style.display==='none';
  panel.style.display=willShow?'block':'none';
  btn.textContent=willShow?'Hide UID / MAC / IP':'Show UID / MAC / IP';
  if(willShow && !deviceInfoLoaded){
    await loadDeviceInfo();
    deviceInfoLoaded=true;
  }
}

async function saveDeviceInfo(){
  const body=JSON.stringify({
    uidName:document.getElementById('uidName').value.trim(),
    ip:document.getElementById('deviceIp').value.trim(),
    ledUid:document.getElementById('ledUid').value.trim(),
    ledMac:document.getElementById('ledMac').value.trim(),
    ledIp:document.getElementById('ledBoardIp').value.trim()
  });
  await fetch('/api/deviceinfo',{method:'POST',headers:{'Content-Type':'application/json'},body});
  await loadDeviceInfo();
  alert('UID/IP updated on board');
}

async function loadCfg(){
  const r=await fetch('/api/config');const j=await r.json();
  document.getElementById("mode").value=j.mode||"OTAA";
  document.getElementById("joinEUI").value=j.joinEUI||"";
  document.getElementById("devEUI").value=j.devEUI||"";
  document.getElementById("appKey").value=j.appKey||"";
  document.getElementById("nwkKey").value=j.nwkKey||"";
  document.getElementById("devAddr").value=j.devAddr||"";
  document.getElementById("nwkSKey").value=j.nwkSKey||"";
  document.getElementById("appSKey").value=j.appSKey||"";
  document.getElementById("region").value=j.region||"EU868";
  document.getElementById("adr").checked=!!j.adr;
  document.getElementById("confirmed").checked=!!j.confirmed;
  document.getElementById("fPort").value=j.fPort||1;
  document.getElementById("txPower").value=j.txPower||14;
  document.getElementById("customPayload").value=j.customPayload||"";
  document.getElementById("intervalSec").value=j.intervalSec||2;
  document.getElementById("intervalMin").value=j.intervalMin||0;
  document.getElementById("intervalHour").value=j.intervalHour||0;
  document.getElementById("intervalDay").value=j.intervalDay||0;
  toggle();
}

async function save(){
  const body=JSON.stringify({
    mode:document.getElementById("mode").value,
    joinEUI:document.getElementById("joinEUI").value.trim(),
    devEUI:document.getElementById("devEUI").value.trim(),
    appKey:document.getElementById("appKey").value.trim(),
    nwkKey:document.getElementById("nwkKey").value.trim(),
    devAddr:document.getElementById("devAddr").value.trim(),
    nwkSKey:document.getElementById("nwkSKey").value.trim(),
    appSKey:document.getElementById("appSKey").value.trim(),
    region:document.getElementById("region").value,
    adr:document.getElementById("adr").checked,
    confirmed:document.getElementById("confirmed").checked,
    fPort:parseInt(document.getElementById("fPort").value)||1,
    txPower:parseInt(document.getElementById("txPower").value)||14,
    customPayload:document.getElementById("customPayload").value,
    intervalSec:parseInt(document.getElementById("intervalSec").value)||2,
    intervalMin:parseInt(document.getElementById("intervalMin").value)||0,
    intervalHour:parseInt(document.getElementById("intervalHour").value)||0,
    intervalDay:parseInt(document.getElementById("intervalDay").value)||0
  });
  await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body});
}

async function clearLogs(){
  const btn=document.getElementById('clearLogsBtn');
  const box=document.getElementById('log');
  if(!btn)return;
  if(btn.disabled)return;

  btn.disabled=true;
  btn.classList.add('is-busy');
  btn.textContent='Clearing…';
  box.innerHTML='Waiting for events…';

  try{
    await fetch('/api/logs/clear',{method:'POST'});
  }catch(e){}

  btn.disabled=false;
  btn.classList.remove('is-busy');
  btn.textContent='Clear logs';
}

async function join(){await fetch('/api/join',{method:'POST'});}
async function leave(){await fetch('/api/leave',{method:'POST'});}
async function forceUplink(){
  await fetch('/api/forceuplink',{method:'POST'});
  alert('Force uplink triggered!');
}

function formatTime(sec){
  if(sec===null||sec===undefined)return'--';
  if(sec<60)return sec+'s';
  if(sec<3600)return Math.floor(sec/60)+'m '+Math.floor(sec%60)+'s';
  return Math.floor(sec/3600)+'h '+Math.floor((sec%3600)/60)+'m';
}

let pollInterval;
function startPolling(){
  if(pollInterval)clearInterval(pollInterval);
  pollInterval=setInterval(async()=>{
    try{
      const rState=await fetch('/api/dlstate');
      const state=await rState.json();
      
      if(state.uplinkSent){
        document.getElementById("uplinkState").innerHTML=`✓ Sent (${state.uplinkPayloadSize}B, port ${state.uplinkFPort})`;
        document.getElementById("dlWindowState").innerHTML='<span style="color:var(--success);">✓ Open — TTN can schedule downlink</span>';
        if(state.downlinkReceived){
          const info=state.hasAppPayload?`Received ${state.downlinkPayloadSize}B on port ${state.downlinkFPort}`:'MAC command only';
          document.getElementById("dlStatus").innerHTML=info;
        }else{
          document.getElementById("dlStatus").innerHTML='<span class="dl-warning">Waiting for TTN scheduling…</span>';
        }
      }
      
      document.getElementById("nextUplinkTimer").textContent=formatTime(state.nextUplinkSec);
      document.getElementById("timeSinceUplink").textContent=formatTime(state.timeSinceUplinkSec);
      
      const rLog=await fetch('/api/logs');
      const jLog=await rLog.json();
      const box=document.getElementById("log");
      const clearBtn=document.getElementById("clearLogsBtn");
      if(!(clearBtn&&clearBtn.disabled)){
        box.innerHTML="";
        for(const e of jLog.logs){
          const line=`[${e.t}][${e.d}] ${e.m}`;
          const span=document.createElement('div');
          if(e.d==='UP')span.className='log-up';
          else if(e.d==='DOWN')span.className='log-down';
          else if(e.d==='ERROR')span.className='log-err';
          span.textContent=line;
          box.appendChild(span);
        }
        box.scrollTop=box.scrollHeight;
      }
      
      const rDl=await fetch('/api/downlink');
      const jDl=await rDl.json();
      const dlBox=document.getElementById("downlinkBox");
      if(!jDl.downlink||jDl.downlink.length===0){
        dlBox.innerHTML='<span class="empty">No downlink yet</span>';
      }else{
        dlBox.innerHTML="";
        for(const e of [...jDl.downlink].reverse()){
          const row=document.createElement("div");
          row.className='dl-entry';
          row.innerHTML=`<span class="dl-time">${e.t}</span><span class="dl-port">PORT ${e.fport}</span><span class="dl-hex">${e.hex||'(empty)'}</span><span class="dl-ascii">${e.ascii||''}</span>`;
          dlBox.appendChild(row);
        }
      }
      
      if(jLog.joined){
        document.getElementById("statusPill").className="status-pill pill-ok";
        document.getElementById("statusTxt").textContent="Joined";
      }else{
        document.getElementById("statusPill").className="status-pill pill-idle";
        document.getElementById("statusTxt").textContent="Not connected";
      }
    }catch(e){}
  },1000);
}
</script>
</body></html>

)HTML";
