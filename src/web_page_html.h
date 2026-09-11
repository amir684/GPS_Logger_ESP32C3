#pragma once

// Markup and styles of the single-page web app. The script lives in web_page_js.h.
static const char kPageHtml[] PROGMEM = R"==(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="dark light">
<title>GPS Logger</title>
<style>
:root{--bg:#0b1016;--panel:#121a24;--panel2:#18222f;--line:#243244;--grid:rgba(140,160,185,.14);--text:#e6edf5;--muted:#8fa1b6;
--accent:#4cc2ff;--accent2:#35d6a0;--warn:#ffb547;--bad:#ff6b6b;--good:#35d6a0;--c1:#4cc2ff;--c2:#b58cff;--c3:#ffb547;--radius:14px}
@media (prefers-color-scheme:light){:root{--bg:#f3f6fa;--panel:#fff;--panel2:#f6f8fb;--line:#dbe3ec;--grid:rgba(40,60,90,.1);
--text:#16202b;--muted:#5d6b7c;--accent:#0a84d6;--accent2:#12a37a;--c1:#0a84d6;--c2:#7b4fd6;--c3:#d98a00}}
*{box-sizing:border-box}
[hidden]{display:none!important}
body{margin:0;background:var(--bg);color:var(--text);font:14px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
header.top{position:sticky;top:0;z-index:5;display:flex;flex-wrap:wrap;gap:10px 18px;align-items:center;justify-content:space-between;
padding:10px 16px;background:color-mix(in srgb,var(--bg) 88%,transparent);backdrop-filter:blur(8px);border-bottom:1px solid var(--line)}
.brand{display:flex;align-items:center;gap:10px;min-width:0}
.brand h1{margin:0;font-size:17px;white-space:nowrap}
.sub{color:var(--muted);font-size:12px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.dot{width:12px;height:12px;border-radius:50%;background:var(--muted);flex:none;box-shadow:0 0 0 3px var(--panel2)}
.dot.rec{background:var(--bad);animation:pulse 1.6s infinite}.dot.idle{background:var(--good)}.dot.off{background:#555}
@keyframes pulse{50%{opacity:.35}}
nav{display:flex;gap:4px;overflow-x:auto;max-width:100%}
nav button{background:none;border:0;color:var(--muted);padding:8px 12px;border-radius:10px;font-weight:600;cursor:pointer;white-space:nowrap}
nav button.active{background:var(--panel2);color:var(--text);box-shadow:inset 0 -2px 0 var(--accent)}
main{max-width:1180px;margin:0 auto;padding:16px}
.tab{display:none}.tab.active{display:block}
.grid{display:grid;gap:12px;margin-bottom:12px}
.kpis{grid-template-columns:repeat(auto-fit,minmax(250px,1fr))}
.two{grid-template-columns:repeat(auto-fit,minmax(min(100%,340px),1fr))}
.card{background:var(--panel);border:1px solid var(--line);border-radius:var(--radius);padding:14px;min-width:0}
.card h3{margin:0 0 10px;font-size:14px;letter-spacing:.2px}
.card-h{display:flex;align-items:center;justify-content:space-between;gap:8px;flex-wrap:wrap;margin-bottom:8px}
.card-h h3{margin:0}
.kpi .label{color:var(--muted);font-size:12px;text-transform:uppercase;letter-spacing:.6px}
.kpi .value{font-size:28px;font-weight:700;font-variant-numeric:tabular-nums;line-height:1.15;margin-top:4px}
.kpi .value small{font-size:13px;color:var(--muted);font-weight:500;margin-left:4px}
.kpi .foot{color:var(--muted);font-size:12px;margin-top:2px}
.kpi.hero .value{font-size:44px}
.muted{color:var(--muted)}.good{color:var(--good)}.bad{color:var(--bad)}.warn{color:var(--warn)}
canvas.chart{width:100%;height:190px;display:block}
canvas.map{width:100%;height:420px;display:block;border-radius:10px;background:var(--panel2);cursor:crosshair}
button,.btn{display:inline-flex;align-items:center;gap:6px;background:var(--panel2);color:var(--text);border:1px solid var(--line);
border-radius:10px;padding:7px 12px;font:600 13px system-ui,sans-serif;cursor:pointer;text-decoration:none;white-space:nowrap}
button:hover,.btn:hover{border-color:var(--accent)}
button:disabled{opacity:.45;cursor:default}
.primary{background:var(--accent);border-color:var(--accent);color:#04121c}
.danger{color:var(--bad);border-color:color-mix(in srgb,var(--bad) 45%,var(--line))}
.btns{display:flex;flex-wrap:wrap;gap:6px}
td .btns{flex-wrap:nowrap}
.table-wrap{overflow-x:auto}
table{width:100%;border-collapse:collapse}
th,td{text-align:left;padding:8px 6px;border-bottom:1px solid var(--line);white-space:nowrap}
th{color:var(--muted);font-weight:600;font-size:12px}
tr.sel td{background:var(--panel2)}
table.kv td:first-child{color:var(--muted);width:42%}
table.kv td{white-space:normal;word-break:break-word}
.tag{display:inline-block;padding:1px 7px;border-radius:99px;font-size:11px;font-weight:700;background:var(--bad);color:#fff;margin-left:6px}
.bar{height:8px;background:var(--panel2);border-radius:99px;overflow:hidden;margin:6px 0}
.bar>div{height:100%;width:0;background:linear-gradient(90deg,var(--accent2),var(--accent));transition:width .4s}
.toolbar{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin-bottom:12px}
.toolbar input[type=search]{flex:1 1 220px}
.spacer{flex:1}
input[type=text],input[type=password],input[type=search],select{background:var(--panel2);color:var(--text);border:1px solid var(--line);
border-radius:9px;padding:7px 9px;font:inherit;width:100%;min-width:0}
input:focus,select:focus{outline:2px solid color-mix(in srgb,var(--accent) 60%,transparent);outline-offset:1px}
.setting{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,48%);gap:10px;align-items:center;padding:9px 6px;border-top:1px solid var(--line)}
.setting:first-of-type{border-top:0}
.setting.dirty{background:color-mix(in srgb,var(--accent) 9%,transparent);border-radius:8px}
.setting.error{background:color-mix(in srgb,var(--bad) 12%,transparent);border-radius:8px}
.s-info label{font-weight:600;display:block}
.s-info small{color:var(--muted);display:block;font-size:12px}
.s-ctl{display:flex;justify-content:flex-end;min-width:0}
.range{display:flex;align-items:center;gap:8px;width:100%;min-width:0}
.range input{flex:1;min-width:0;width:100%;accent-color:var(--accent)}
.range output{flex:none;min-width:56px;text-align:right;font-variant-numeric:tabular-nums;color:var(--muted)}
.switch{position:relative;width:44px;height:24px;flex:none}
.switch input{opacity:0;width:0;height:0}
.switch span{position:absolute;inset:0;background:var(--line);border-radius:99px;transition:.2s;cursor:pointer}
.switch span:before{content:"";position:absolute;width:18px;height:18px;left:3px;top:3px;background:#fff;border-radius:50%;transition:.2s}
.switch input:checked+span{background:var(--accent)}
.switch input:checked+span:before{transform:translateX(20px)}
.legend{display:flex;align-items:center;gap:8px;margin:8px 0;color:var(--muted);font-size:12px}
.legend .grad{flex:1;height:8px;border-radius:99px;background:linear-gradient(90deg,hsl(0 95% 50%),hsl(60 95% 50%),hsl(120 95% 45%),hsl(180 95% 45%),hsl(240 95% 60%),hsl(300 95% 55%))}
.maptip{position:fixed;pointer-events:none;background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:6px 8px;font-size:12px;
box-shadow:0 6px 20px rgba(0,0,0,.35);z-index:20}
#toast{position:fixed;left:50%;bottom:22px;transform:translateX(-50%) translateY(20px);opacity:0;transition:.25s;background:var(--panel);
border:1px solid var(--line);padding:10px 16px;border-radius:12px;box-shadow:0 10px 30px rgba(0,0,0,.35);z-index:30;max-width:90vw}
#toast.show{opacity:1;transform:translateX(-50%)}
#toast.bad{border-color:var(--bad)}
.savebar{position:sticky;bottom:12px;display:flex;gap:8px;align-items:center;justify-content:flex-end;background:var(--panel);
border:1px solid var(--line);border-radius:12px;padding:8px 10px;box-shadow:0 8px 24px rgba(0,0,0,.3);margin-top:8px}
.empty{padding:26px;text-align:center;color:var(--muted)}
canvas.lcd{width:100%;max-width:520px;aspect-ratio:2/1;display:block;margin:0 auto 10px;image-rendering:pixelated;
border-radius:8px;border:6px solid #2b3440;background:#b9c9a8}
.remote{justify-content:center}
.remote button{min-width:74px;justify-content:center}
@media (max-width:560px){.kpis{grid-template-columns:repeat(2,minmax(0,1fr))}.kpi .value{font-size:22px}.kpi.hero .value{font-size:32px}.setting{grid-template-columns:1fr}.s-ctl{justify-content:flex-start}canvas.map{height:320px}}
</style>
</head>
<body>
<header class="top">
  <div class="brand"><span class="dot" id="recDot"></span>
    <div style="min-width:0"><h1 id="devName">GPS Logger</h1><div class="sub" id="subline">connecting...</div></div></div>
  <nav id="tabs">
    <button data-tab="dash" class="active">Dashboard</button>
    <button data-tab="sessions">Sessions</button>
    <button data-tab="settings">Settings</button>
    <button data-tab="system">System</button>
  </nav>
</header>

<main>
<section id="tab-dash" class="tab active">
  <div class="grid kpis">
    <div class="card kpi hero"><div class="label">Speed</div><div class="value"><span id="kSpeed">--</span><small id="kSpeedU"></small></div><div class="foot" id="kSpeedFoot"></div></div>
    <div class="card kpi"><div class="label">Trip distance</div><div class="value"><span id="kDist">--</span><small id="kDistU"></small></div><div class="foot" id="kDistFoot"></div></div>
    <div class="card kpi"><div class="label">Moving time</div><div class="value" id="kMoving">--</div><div class="foot" id="kMovingFoot"></div></div>
    <div class="card kpi"><div class="label">Altitude</div><div class="value"><span id="kAlt">--</span><small id="kAltU"></small></div><div class="foot" id="kAltFoot"></div></div>
    <div class="card kpi"><div class="label">GPS</div><div class="value" id="kSats">--</div><div class="foot" id="kFix"></div></div>
    <div class="card kpi"><div class="label">Battery</div><div class="value" id="kBat">--</div><div class="foot" id="kBatFoot"></div></div>
    <div class="card kpi"><div class="label">Logging</div><div class="value" id="kLog">--</div><div class="foot" id="kLogFoot"></div></div>
    <div class="card kpi"><div class="label">Storage</div><div class="value" id="kStore">--</div><div class="bar"><div id="kStoreBar"></div></div><div class="foot" id="kStoreFoot"></div></div>
  </div>
  <div class="grid two">
    <div class="card"><div class="card-h"><h3>Speed</h3><span class="muted" id="spanSpeed"></span></div><canvas id="chSpeed" class="chart"></canvas></div>
    <div class="card"><div class="card-h"><h3>Altitude</h3><span class="muted" id="spanAlt"></span></div><canvas id="chAlt" class="chart"></canvas></div>
    <div class="card"><div class="card-h"><h3>Battery</h3><span class="muted" id="spanBat"></span></div><canvas id="chBat" class="chart"></canvas></div>
    <div class="card"><div class="card-h"><h3>Position</h3><a class="btn" id="mapLink" target="_blank" rel="noopener" hidden>Open map</a></div><table class="kv" id="gpsTable"></table></div>
    <div class="card"><div class="card-h"><h3>Device screen</h3><span class="muted">live mirror and remote</span></div>
      <canvas id="lcd" class="lcd" width="128" height="64"></canvas>
      <div class="btns remote"><button data-key="up">&#9650; Up</button><button data-key="down">&#9660; Down</button>
        <button data-key="push">&#9679; Push</button><button data-key="hold">Hold</button></div></div>
  </div>
  <div class="card"><div class="card-h"><h3>Quick actions</h3></div>
    <div class="btns"><button id="btnLogging">Logging</button><button id="btnNewSession">New session</button><button id="btnTripReset">Reset trip</button><button id="btnSleep">Sleep</button></div></div>
</section>

<section id="tab-sessions" class="tab">
  <div class="card">
    <div class="card-h"><h3>Sessions</h3><div class="btns"><button id="btnSessionsRefresh">Refresh</button><button class="danger" id="btnEraseAll">Erase all</button></div></div>
    <div class="bar"><div id="storeBar"></div></div><div class="muted" id="storeText"></div>
    <div class="table-wrap"><table id="sessionTable"><thead><tr><th>Start</th><th>Duration</th><th>Points</th><th>Size</th><th>Download</th><th></th></tr></thead><tbody></tbody></table></div>
  </div>
  <div class="card" id="trackCard" hidden>
    <div class="card-h"><h3 id="trackTitle">Track</h3><div class="btns" id="trackDownloads"></div></div>
    <div class="grid kpis" id="trackStats"></div>
    <canvas id="trackMap" class="map"></canvas>
    <div class="legend"><span id="legendMin">0</span><div class="grad"></div><span id="legendMax"></span></div>
    <div class="card-h" style="margin-top:12px"><h3>Speed and altitude profile</h3></div>
    <canvas id="trackProfile" class="chart"></canvas>
  </div>
</section>

<section id="tab-settings" class="tab">
  <div class="toolbar">
    <input type="search" id="settingsFilter" placeholder="Search settings...">
    <button id="btnBackup">Backup</button>
    <label class="btn">Restore<input type="file" id="fileRestore" accept=".json,application/json" hidden></label>
  </div>
  <div id="settingsForm" class="grid two"><div class="card empty">Loading settings...</div></div>
  <div class="savebar" id="saveBar" hidden><span class="muted" id="saveInfo"></span><span class="spacer"></span>
    <button id="btnRevert">Revert</button><button class="primary" id="btnSave">Save changes</button></div>
</section>

<section id="tab-system" class="tab">
  <div class="grid two">
    <div class="card"><h3>Device</h3><table class="kv" id="sysTable"></table></div>
    <div class="card"><h3>Network</h3><table class="kv" id="netTable"></table></div>
    <div class="card"><h3>Firmware update</h3>
      <p class="muted">Upload <code>.pio/build/esp32c3_16mb/firmware.bin</code>. Settings and logs are kept.</p>
      <div class="btns"><input type="file" id="fwFile" accept=".bin"><button class="primary" id="btnFlash">Upload</button></div>
      <div class="bar"><div id="fwBar"></div></div><div class="muted" id="fwStatus"></div></div>
    <div class="card"><h3>Maintenance</h3>
      <div class="btns">
        <button data-action="restart">Restart</button>
        <button data-action="sleep">Sleep</button>
        <button data-action="wifi_off">WiFi off</button>
        <button class="danger" data-action="erase_logs">Erase logs</button>
        <button class="danger" data-action="factory_reset">Factory reset</button>
      </div>
      <p class="muted">Sleep powers the logger down; push the jog wheel to wake it.</p></div>
  </div>
</section>
</main>
<div id="toast"></div>
<div class="maptip" id="mapTip" hidden></div>
)==";
