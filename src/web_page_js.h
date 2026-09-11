#pragma once

// Script of the single-page web app (sent right after kPageHtml)
static const char kPageJs[] PROGMEM = R"==(<script>
'use strict';
const $ = (s, root = document) => root.querySelector(s);
const $$ = (s, root = document) => [...root.querySelectorAll(s)];
const pad = n => String(n).padStart(2, '0');
const esc = s => String(s ?? '').replace(/[&<>"']/g, c => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'}[c]));
const css = v => getComputedStyle(document.documentElement).getPropertyValue(v).trim();
const WIFI_KEYS = ['wifi_mode', 'ap_ssid', 'ap_pass', 'sta_ssid', 'sta_pass', 'hostname', 'wifi_txpwr'];

let status = null;
let units = {speed: 'km/h', speedF: 1, dist: 'km', distF: 0.001, alt: 'm', altF: 1};
let activeTab = 'dash';
let settingsData = null;
let pending = {};
let history = null;
let lastHistoryMs = 0;
let selectedSession = '';

const fmt = {
  dur(s) {
    s = Math.max(0, Math.round(s));
    const h = Math.floor(s / 3600), m = Math.floor(s % 3600 / 60), sec = s % 60;
    return h ? `${h}:${pad(m)}:${pad(sec)}` : `${m}:${pad(sec)}`;
  },
  span(s) { return s < 120 ? `${Math.round(s)}s` : s < 7200 ? `${Math.round(s / 60)}m` : `${(s / 3600).toFixed(1)}h`; },
  bytes(b) { return b >= 1048576 ? `${(b / 1048576).toFixed(2)} MB` : `${(b / 1024).toFixed(1)} KB`; },
  // Fixed numeric format: locale strings mix right-to-left text into the tables
  date(epoch) {
    const d = new Date(epoch * 1000);
    return `${pad(d.getDate())}.${pad(d.getMonth() + 1)}.${d.getFullYear()} ${pad(d.getHours())}:${pad(d.getMinutes())}`;
  },
  uptime(s) {
    const d = Math.floor(s / 86400);
    return (d ? `${d}d ` : '') + `${pad(Math.floor(s % 86400 / 3600))}:${pad(Math.floor(s % 3600 / 60))}:${pad(s % 60)}`;
  },
};

function toast(message, bad = false) {
  const t = $('#toast');
  t.textContent = message;
  t.classList.toggle('bad', bad);
  t.classList.add('show');
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => t.classList.remove('show'), 2800);
}

async function api(path, options) {
  const response = await fetch(path, options);
  const text = await response.text();
  let data = {};
  try { data = text ? JSON.parse(text) : {}; } catch (e) { data = {raw: text}; }
  if (!response.ok) throw new Error(data.error || response.statusText || 'request failed');
  return data;
}

function post(path, body) {
  const options = {method: 'POST'};
  if (body !== undefined) {
    options.headers = {'Content-Type': 'application/json'};
    options.body = JSON.stringify(body);
  }
  return api(path, options);
}

function setText(id, text) {
  const el = document.getElementById(id);
  if (el) el.textContent = text;
}

function kvTable(id, rows) {
  $('#' + id).innerHTML = rows.map(([k, v]) => `<tr><td>${esc(k)}</td><td>${v}</td></tr>`).join('');
}

// ------------------------------------------------------------------ tabs

function showTab(tab) {
  activeTab = tab;
  $$('#tabs button').forEach(b => b.classList.toggle('active', b.dataset.tab === tab));
  $$('.tab').forEach(s => s.classList.toggle('active', s.id === 'tab-' + tab));
  if (location.hash !== '#' + tab) history_replace(tab);
  if (tab === 'sessions') loadSessions();
  if (tab === 'settings' && !settingsData) loadSettings();
  if (tab === 'system') renderSystem();
  if (tab === 'dash') { lastHistoryMs = 0; renderCharts(); }
}

function history_replace(tab) {
  try { window.history.replaceState(null, '', '#' + tab); } catch (e) { /* file:// or old browser */ }
}

$$('#tabs button').forEach(b => b.addEventListener('click', () => showTab(b.dataset.tab)));

// ------------------------------------------------------------------ charts

function setupCanvas(canvas) {
  const rect = canvas.getBoundingClientRect();
  const ratio = window.devicePixelRatio || 1;
  canvas.width = Math.max(1, Math.round(rect.width * ratio));
  canvas.height = Math.max(1, Math.round(rect.height * ratio));
  const g = canvas.getContext('2d');
  g.setTransform(ratio, 0, 0, ratio, 0, 0);
  return [g, rect.width, rect.height];
}

function lineChart(canvas, values, opt) {
  const [g, w, h] = setupCanvas(canvas);
  g.clearRect(0, 0, w, h);
  g.font = '11px system-ui, sans-serif';
  g.fillStyle = css('--muted');
  if (!values || values.length < 2) {
    g.fillText('collecting data...', w / 2 - 45, h / 2);
    return;
  }
  const L = 46, R = 10, T = 10, B = 22;
  let lo = Math.min(...values), hi = Math.max(...values);
  if (hi - lo < 1e-6) { lo -= 1; hi += 1; }
  const margin = (hi - lo) * 0.08;
  lo -= margin; hi += margin;
  const X = i => L + i * (w - L - R) / (values.length - 1);
  const Y = v => T + (hi - v) * (h - T - B) / (hi - lo);

  g.strokeStyle = css('--grid');
  g.lineWidth = 1;
  for (let k = 0; k <= 4; k++) {
    const v = lo + (hi - lo) * k / 4, y = Y(v);
    g.beginPath(); g.moveTo(L, y); g.lineTo(w - R, y); g.stroke();
    g.fillText(v.toFixed(opt.dec), 4, y + 4);
  }
  if (opt.xLabel) {
    g.fillText(opt.xLabel(0), L, h - 6);
    const last = opt.xLabel(values.length - 1);
    g.fillText(last, w - R - g.measureText(last).width, h - 6);
  }

  g.beginPath();
  values.forEach((v, i) => (i ? g.lineTo(X(i), Y(v)) : g.moveTo(X(i), Y(v))));
  g.strokeStyle = opt.color;
  g.lineWidth = 2;
  g.lineJoin = 'round';
  g.stroke();
  g.lineTo(X(values.length - 1), h - B);
  g.lineTo(X(0), h - B);
  g.closePath();
  const fill = g.createLinearGradient(0, T, 0, h - B);
  fill.addColorStop(0, opt.color + '55');
  fill.addColorStop(1, opt.color + '00');
  g.fillStyle = fill;
  g.fill();

  const lastValue = values[values.length - 1];
  g.fillStyle = opt.color;
  g.beginPath(); g.arc(X(values.length - 1), Y(lastValue), 3.5, 0, Math.PI * 2); g.fill();
}

function renderCharts() {
  if (!history || activeTab !== 'dash') return;
  const age = (n, period) => i => (i === n - 1 ? 'now' : '-' + fmt.span((n - 1 - i) * period));
  const speed = history.speed.map(v => v * units.speedF);
  const alt = history.alt.map(v => v * units.altF);
  lineChart($('#chSpeed'), speed, {color: css('--c1'), dec: 1, xLabel: age(speed.length, history.speedPeriod)});
  lineChart($('#chAlt'), alt, {color: css('--c2'), dec: 0, xLabel: age(alt.length, history.speedPeriod)});
  lineChart($('#chBat'), history.battery, {color: css('--c3'), dec: 2, xLabel: age(history.battery.length, history.batteryPeriod)});
  setText('spanSpeed', `${units.speed}, last ${fmt.span(Math.max(0, speed.length - 1) * history.speedPeriod)}`);
  setText('spanAlt', `${units.alt}, last ${fmt.span(Math.max(0, alt.length - 1) * history.speedPeriod)}`);
  setText('spanBat', `volts, last ${fmt.span(Math.max(0, history.battery.length - 1) * history.batteryPeriod)}`);
}

async function loadHistory() {
  try {
    history = await api('/api/history');
    lastHistoryMs = Date.now();
    renderCharts();
  } catch (e) { /* status poll reports connectivity */ }
}

// ------------------------------------------------------------------ status

function renderHeader() {
  const s = status;
  setText('devName', s.name || 'GPS Logger');
  document.title = s.name || 'GPS Logger';
  const ip = s.wifi.sta || s.wifi.ap;
  setText('subline', `${s.local || 'waiting for GPS time'}  |  ${s.wifi.mode}${ip ? ' ' + ip : ''}  |  up ${fmt.uptime(s.uptime)}`);
  const dot = $('#recDot');
  dot.className = 'dot ' + (s.log.on && s.gps.timeValid && !s.log.full ? 'rec' : s.log.on ? 'idle' : 'off');
  dot.title = s.log.on ? (s.log.full ? 'storage full' : 'logging') : 'logging off';
}

function renderDashboard() {
  const s = status, g = s.gps, t = s.trip;
  setText('kSpeed', g.fix ? (g.speed * units.speedF).toFixed(1) : '--');
  setText('kSpeedU', units.speed);
  setText('kSpeedFoot', `max ${(t.max * units.speedF).toFixed(1)} | avg ${(t.avg * units.speedF).toFixed(1)} | sd ${(t.sd * units.speedF).toFixed(1)}`);
  setText('kDist', (t.distance * units.distF).toFixed(2));
  setText('kDistU', units.dist);
  setText('kDistFoot', `climb ${(t.climb * units.altF).toFixed(0)} ${units.alt} | descent ${(t.descent * units.altF).toFixed(0)} ${units.alt}`);
  setText('kMoving', fmt.dur(t.moving));
  setText('kMovingFoot', `${t.samples} moving samples`);
  setText('kAlt', g.fix ? (g.alt * units.altF).toFixed(0) : '--');
  setText('kAltU', units.alt);
  setText('kAltFoot', t.samples ? `range ${(t.altMin * units.altF).toFixed(0)} - ${(t.altMax * units.altF).toFixed(0)} ${units.alt}` : 'no trip data');
  setText('kSats', `${g.sats} sats`);
  $('#kFix').innerHTML = g.fix ? `<span class="good">3D fix</span> | HDOP ${g.hdop.toFixed(1)}` : `<span class="warn">no fix</span> | ${g.chars} NMEA chars`;

  const b = s.battery;
  if (b.present) {
    setText('kBat', `${b.pct}%`);
    $('#kBatFoot').innerHTML = `${b.v.toFixed(2)} V${b.low ? ' <span class="bad">LOW</span>' : ''}${b.runtime >= 0 ? ` | ~${Math.floor(b.runtime / 60)}h ${pad(b.runtime % 60)}m` : ''}`;
  } else {
    setText('kBat', 'USB');
    setText('kBatFoot', 'no cell connected');
  }

  const l = s.log;
  $('#kLog').innerHTML = !l.on ? '<span class="muted">OFF</span>' : l.full ? '<span class="bad">FULL</span>' : '<span class="good">ON</span>';
  setText('kLogFoot', l.on ? `every ${l.interval}s | ${l.records} pts${l.session ? ' | ' + l.session : ''}` : 'press Logging to start');
  setText('btnLogging', l.on ? 'Stop logging' : 'Start logging');

  const pct = l.total ? l.used * 100 / l.total : 0;
  setText('kStore', `${pct.toFixed(1)}%`);
  $('#kStoreBar').style.width = pct + '%';
  setText('kStoreFoot', `${fmt.bytes(l.used)} of ${fmt.bytes(l.total)} | ${l.sessions} sessions`);

  kvTable('gpsTable', [
    ['Latitude', g.fix ? g.lat.toFixed(6) : '-'],
    ['Longitude', g.fix ? g.lon.toFixed(6) : '-'],
    ['Course', g.fix ? `${g.course.toFixed(0)}&deg;` : '-'],
    ['Fix age', g.age >= 0 ? `${(g.age / 1000).toFixed(1)} s` : '-'],
    ['GPS time', g.timeValid ? esc(s.local || '') : 'not synced'],
    ['NMEA', `${g.chars} chars | ${g.passed} ok | ${g.failed} bad`],
  ]);
  const link = $('#mapLink');
  link.hidden = !g.fix;
  if (g.fix) link.href = `https://www.google.com/maps?q=${g.lat.toFixed(6)},${g.lon.toFixed(6)}`;
}

function renderSystem() {
  if (!status) return;
  const s = status, y = s.sys, w = s.wifi;
  kvTable('sysTable', [
    ['Name', esc(s.name)],
    ['Firmware', `${esc(s.fw)} <span class="muted">(${esc(y.build)})</span>`],
    ['Chip', `${esc(y.chip)} | ${y.cpu} MHz`],
    ['Flash', `${fmt.bytes(y.flash)} | app ${fmt.bytes(y.sketch)} (${fmt.bytes(y.sketchFree)} free)`],
    ['SDK', esc(y.sdk)],
    ['Uptime', fmt.uptime(s.uptime)],
    ['Memory', `${fmt.bytes(y.heap)} free | min ${fmt.bytes(y.minHeap)}`],
    ['Chip temperature', y.temp == null ? '-' : `${Number(y.temp).toFixed(1)} &deg;C`],
    ['Last reset', esc(y.reset)],
    ['Logs', `${fmt.bytes(s.log.used)} / ${fmt.bytes(s.log.total)} | ${s.log.sessions} sessions`],
  ]);
  kvTable('netTable', [
    ['Mode', esc(w.mode)],
    ['Access point', w.ap ? `${esc(w.ap)} | ${w.clients} client${w.clients === 1 ? '' : 's'}` : '-'],
    ['Home network', w.sta ? `${esc(w.sta)} | ${w.rssi} dBm` : 'not connected'],
    ['Hostname', w.sta ? `<a href="http://${esc(w.host)}.local/">${esc(w.host)}.local</a>` : esc(w.host)],
    ['This page', esc(location.host)],
  ]);
}

async function pollStatus() {
  try {
    status = await api('/api/status');
    units = status.units;
    renderHeader();
    if (activeTab === 'dash') {
      renderDashboard();
      if (Date.now() - lastHistoryMs > 5000) loadHistory();
    }
    if (activeTab === 'system') renderSystem();
  } catch (e) {
    setText('subline', 'offline - reconnecting...');
    $('#recDot').className = 'dot off';
  }
  setTimeout(pollStatus, document.hidden ? 5000 : activeTab === 'dash' || activeTab === 'system' ? 1000 : 4000);
}

async function action(name, message) {
  try {
    await post('/api/action?do=' + encodeURIComponent(name));
    toast(message);
  } catch (e) {
    toast('Failed: ' + e.message, true);
  }
}

$('#btnLogging').addEventListener('click', () => {
  const on = !(status && status.log.on);
  action(on ? 'logging_on' : 'logging_off', on ? 'Logging started' : 'Logging stopped');
});
$('#btnNewSession').addEventListener('click', () => action('new_session', 'Next sample starts a new session'));
$('#btnTripReset').addEventListener('click', () => confirm('Reset trip statistics?') && action('trip_reset', 'Trip reset'));
$('#btnSleep').addEventListener('click', () => confirm('Put the logger to sleep? Push the jog wheel to wake it.') && action('sleep', 'Going to sleep'));

const ACTION_TEXT = {
  restart: ['Restart the logger?', 'Restarting...'],
  sleep: ['Put the logger to sleep? Push the jog wheel to wake it.', 'Going to sleep'],
  wifi_off: ['Turn WiFi off? This page will disconnect.', 'WiFi turning off'],
  erase_logs: ['Delete ALL logged sessions? This cannot be undone.', 'All sessions deleted'],
  factory_reset: ['Restore all settings to defaults and restart?', 'Factory reset, restarting...'],
};
$$('[data-action]').forEach(b => b.addEventListener('click', () => {
  const [question, done] = ACTION_TEXT[b.dataset.action];
  if (confirm(question)) action(b.dataset.action, done);
}));

// ------------------------------------------------------------------ device screen mirror

let lcdBusy = false;
async function loadLcd() {
  if (lcdBusy) return;
  lcdBusy = true;
  try {
    const d = await api('/api/lcd');
    const bytes = new Uint8Array(d.hex.length / 2);
    for (let i = 0; i < bytes.length; i++) bytes[i] = parseInt(d.hex.substr(i * 2, 2), 16);
    const canvas = $('#lcd');
    const g = canvas.getContext('2d');
    const img = g.createImageData(128, 64);
    for (let y = 0; y < 64; y++) {
      for (let x = 0; x < 128; x++) {
        const on = ((bytes[(y >> 3) * 128 + x] >> (y & 7)) & 1) !== (d.invert ? 1 : 0);
        const vx = d.flip ? 127 - x : x, vy = d.flip ? 63 - y : y;
        const i = (vy * 128 + vx) * 4;
        img.data[i] = on ? 28 : 185;
        img.data[i + 1] = on ? 38 : 201;
        img.data[i + 2] = on ? 30 : 168;
        img.data[i + 3] = 255;
      }
    }
    g.putImageData(img, 0, 0);
  } catch (e) { /* offline: status poll shows it */ }
  lcdBusy = false;
}

$$('[data-key]').forEach(b => b.addEventListener('click', async () => {
  try {
    await post('/api/key?k=' + b.dataset.key);
    setTimeout(loadLcd, 350);
  } catch (e) {
    toast('Key failed: ' + e.message, true);
  }
}));

// ------------------------------------------------------------------ sessions

async function loadSessions() {
  const body = $('#sessionTable tbody');
  try {
    const d = await api('/api/sessions');
    const pct = d.total ? d.used * 100 / d.total : 0;
    $('#storeBar').style.width = pct + '%';
    setText('storeText', `${fmt.bytes(d.used)} of ${fmt.bytes(d.total)} used (${pct.toFixed(1)}%) | ${d.sessions.length} sessions`);
    if (!d.sessions.length) {
      body.innerHTML = '<tr><td colspan="6" class="empty">No sessions yet. Logging starts once GPS time is known.</td></tr>';
      return;
    }
    body.innerHTML = d.sessions.map(s => {
      const n = encodeURIComponent(s.name);
      const dl = f => `<a class="btn" href="/dl?fmt=${f}&f=${n}">${f.toUpperCase()}</a>`;
      return `<tr data-name="${esc(s.name)}" class="${s.name === selectedSession ? 'sel' : ''}">
        <td>${s.records ? esc(fmt.date(s.start)) : '<span class="muted">empty</span>'}${s.recording ? '<span class="tag">REC</span>' : ''}</td>
        <td>${fmt.dur(s.end - s.start)}</td><td>${s.records}</td><td>${fmt.bytes(s.bytes)}</td>
        <td><div class="btns"><button data-view="${esc(s.name)}">View</button>${dl('kml')}${dl('gpx')}${dl('csv')}</div></td>
        <td><button class="danger" data-del="${esc(s.name)}">Delete</button></td></tr>`;
    }).join('');
    $$('[data-view]', body).forEach(b => b.addEventListener('click', () => loadTrack(b.dataset.view)));
    $$('[data-del]', body).forEach(b => b.addEventListener('click', () => deleteSession(b.dataset.del)));
  } catch (e) {
    body.innerHTML = `<tr><td colspan="6" class="empty">Could not load sessions: ${esc(e.message)}</td></tr>`;
  }
}

async function deleteSession(name) {
  if (!confirm(`Delete session ${name}?`)) return;
  try {
    await post('/api/delete?f=' + encodeURIComponent(name));
    toast('Session deleted');
    if (name === selectedSession) $('#trackCard').hidden = true;
    loadSessions();
  } catch (e) {
    toast('Delete failed: ' + e.message, true);
  }
}

$('#btnSessionsRefresh').addEventListener('click', loadSessions);
$('#btnEraseAll').addEventListener('click', async () => {
  if (!confirm('Delete ALL sessions? This cannot be undone.')) return;
  await action('erase_logs', 'All sessions deleted');
  $('#trackCard').hidden = true;
  loadSessions();
});

const speedColor = (kmh, vmax) => `hsl(${Math.round(300 * Math.max(0, Math.min(1, kmh / vmax)))} 95% 50%)`;
let track = null;

async function loadTrack(name) {
  selectedSession = name;
  $$('#sessionTable tr[data-name]').forEach(r => r.classList.toggle('sel', r.dataset.name === name));
  const card = $('#trackCard');
  card.hidden = false;
  setText('trackTitle', `Session ${name}`);
  const n = encodeURIComponent(name);
  $('#trackDownloads').innerHTML = ['kml', 'gpx', 'csv'].map(f => `<a class="btn" href="/dl?fmt=${f}&f=${n}">${f.toUpperCase()}</a>`).join('');
  $('#trackStats').innerHTML = '<div class="card empty">Loading track...</div>';
  card.scrollIntoView({behavior: 'smooth', block: 'start'});
  try {
    track = await api(`/api/track?f=${n}&max=2000`);
  } catch (e) {
    $('#trackStats').innerHTML = `<div class="card empty">Could not load track: ${esc(e.message)}</div>`;
    return;
  }
  const s = track.summary;
  const kpi = (label, value, foot = '') => `<div class="card kpi"><div class="label">${label}</div><div class="value">${value}</div><div class="foot">${foot}</div></div>`;
  $('#trackStats').innerHTML = s.points ? [
    kpi('Distance', `${(s.distance * units.distF).toFixed(2)}<small>${units.dist}</small>`, `${s.points} fixes`),
    kpi('Duration', fmt.dur(s.end - s.start), `moving ${fmt.dur(s.moving)}`),
    kpi('Average', `${(s.avgKmh * units.speedF).toFixed(1)}<small>${units.speed}</small>`, 'while moving'),
    kpi('Top speed', `${(s.maxKmh * units.speedF).toFixed(1)}<small>${units.speed}</small>`, ''),
    kpi('Altitude', `${(s.maxAlt * units.altF).toFixed(0)}<small>${units.alt}</small>`, `min ${(s.minAlt * units.altF).toFixed(0)} ${units.alt}`),
    kpi('Started', fmt.date(s.start).slice(11), fmt.date(s.start).slice(0, 10)),
  ].join('') : '<div class="card empty">This session has no GPS fixes (logged without position).</div>';
  setText('legendMin', `0 ${units.speed}`);
  setText('legendMax', `${(s.colorMaxKmh * units.speedF).toFixed(0)}+ ${units.speed}`);
  drawTrack();
  drawProfile();
}

function drawTrack() {
  const canvas = $('#trackMap');
  const [g, w, h] = setupCanvas(canvas);
  g.clearRect(0, 0, w, h);
  track && (track.screen = []);
  if (!track || track.points.length < 2) {
    g.fillStyle = css('--muted');
    g.font = '13px system-ui, sans-serif';
    g.fillText('No track to draw', w / 2 - 50, h / 2);
    return;
  }
  const pts = track.points;
  let minLat = Infinity, maxLat = -Infinity, minLon = Infinity, maxLon = -Infinity;
  for (const p of pts) {
    minLat = Math.min(minLat, p[0]); maxLat = Math.max(maxLat, p[0]);
    minLon = Math.min(minLon, p[1]); maxLon = Math.max(maxLon, p[1]);
  }
  const midLat = (minLat + maxLat) / 2;
  const kx = Math.cos(midLat * Math.PI / 180) * 111320, ky = 110540;  // meters per degree
  const spanX = Math.max((maxLon - minLon) * kx, 20), spanY = Math.max((maxLat - minLat) * ky, 20);
  const P = 28;
  const scale = Math.min((w - 2 * P) / spanX, (h - 2 * P) / spanY);
  const cx = (minLon + maxLon) / 2, cy = midLat;
  const X = lon => w / 2 + (lon - cx) * kx * scale;
  const Y = lat => h / 2 - (lat - cy) * ky * scale;

  g.strokeStyle = css('--grid');
  g.lineWidth = 1;
  for (let x = 0; x < w; x += 40) { g.beginPath(); g.moveTo(x, 0); g.lineTo(x, h); g.stroke(); }
  for (let y = 0; y < h; y += 40) { g.beginPath(); g.moveTo(0, y); g.lineTo(w, y); g.stroke(); }

  const vmax = track.summary.colorMaxKmh || 5;
  g.lineCap = 'round';
  g.lineWidth = 3.5;
  for (let i = 1; i < pts.length; i++) {
    const a = pts[i - 1], b = pts[i];
    if (b[4] - a[4] > 300) continue;  // logging gap
    g.strokeStyle = speedColor((a[2] + b[2]) / 2, vmax);
    g.beginPath(); g.moveTo(X(a[1]), Y(a[0])); g.lineTo(X(b[1]), Y(b[0])); g.stroke();
  }
  track.screen = pts.map(p => [X(p[1]), Y(p[0])]);

  const marker = (p, color, label, dy) => {
    g.fillStyle = color; g.strokeStyle = '#fff'; g.lineWidth = 2;
    g.beginPath(); g.arc(X(p[1]), Y(p[0]), 7, 0, Math.PI * 2); g.fill(); g.stroke();
    g.fillStyle = css('--text'); g.font = '600 12px system-ui, sans-serif';
    const textWidth = g.measureText(label).width;
    const x = X(p[1]) + 11 + textWidth > w - 4 ? X(p[1]) - 11 - textWidth : X(p[1]) + 11;  // keep inside the canvas
    g.fillText(label, x, Y(p[0]) + dy);
  };
  marker(pts[0], '#23c26b', 'Start', -4);
  marker(pts[pts.length - 1], '#ff5a5a', 'End', 14);  // below Start when a loop ends where it began

  // scale bar
  const metersPerPx = 1 / scale;
  const nice = [10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000];
  const meters = nice.find(m => m / metersPerPx > 70) || nice[nice.length - 1];
  const len = meters / metersPerPx;
  g.strokeStyle = css('--text'); g.lineWidth = 2;
  g.beginPath(); g.moveTo(14, h - 14); g.lineTo(14 + len, h - 14); g.stroke();
  g.fillStyle = css('--text'); g.font = '11px system-ui, sans-serif';
  const scaleText = units.dist === 'km' ? (meters >= 1000 ? `${meters / 1000} km` : `${meters} m`) : `${(meters * units.distF).toFixed(2)} ${units.dist}`;
  g.fillText(scaleText, 14, h - 20);
}

function drawProfile() {
  const canvas = $('#trackProfile');
  const [g, w, h] = setupCanvas(canvas);
  g.clearRect(0, 0, w, h);
  if (!track || track.points.length < 2) return;
  const pts = track.points;
  const L = 46, R = 46, T = 10, B = 22;
  const tMax = Math.max(1, pts[pts.length - 1][4]);
  const X = t => L + t * (w - L - R) / tMax;
  const speeds = pts.map(p => p[2] * units.speedF), alts = pts.map(p => p[3] * units.altF);
  const sMax = Math.max(1, ...speeds);
  let aMin = Math.min(...alts), aMax = Math.max(...alts);
  if (aMax - aMin < 5) { aMin -= 5; aMax += 5; }
  const Ys = v => T + (1 - v / sMax) * (h - T - B);
  const Ya = v => T + (aMax - v) * (h - T - B) / (aMax - aMin);

  g.font = '11px system-ui, sans-serif';
  g.strokeStyle = css('--grid');
  for (let k = 0; k <= 4; k++) {
    const y = T + k * (h - T - B) / 4;
    g.beginPath(); g.moveTo(L, y); g.lineTo(w - R, y); g.stroke();
    g.fillStyle = css('--c1'); g.fillText((sMax * (1 - k / 4)).toFixed(0), 4, y + 4);
    g.fillStyle = css('--c2'); g.fillText((aMax - (aMax - aMin) * k / 4).toFixed(0), w - R + 6, y + 4);
  }
  g.fillStyle = css('--muted');
  g.fillText('0:00', L, h - 6);
  const end = fmt.dur(tMax);
  g.fillText(end, w - R - g.measureText(end).width, h - 6);

  g.beginPath();
  pts.forEach((p, i) => (i ? g.lineTo(X(p[4]), Ya(alts[i])) : g.moveTo(X(p[4]), Ya(alts[i]))));
  g.lineTo(X(tMax), h - B); g.lineTo(X(0), h - B); g.closePath();
  g.fillStyle = css('--c2') + '33';
  g.fill();

  g.beginPath();
  pts.forEach((p, i) => (i ? g.lineTo(X(p[4]), Ys(speeds[i])) : g.moveTo(X(p[4]), Ys(speeds[i]))));
  g.strokeStyle = css('--c1'); g.lineWidth = 1.6;
  g.stroke();
}

$('#trackMap').addEventListener('mousemove', e => {
  const tip = $('#mapTip');
  if (!track || !track.screen || !track.screen.length) { tip.hidden = true; return; }
  const rect = e.target.getBoundingClientRect();
  const mx = e.clientX - rect.left, my = e.clientY - rect.top;
  let best = -1, bestD = 196;
  track.screen.forEach(([x, y], i) => {
    const d = (x - mx) ** 2 + (y - my) ** 2;
    if (d < bestD) { bestD = d; best = i; }
  });
  if (best < 0) { tip.hidden = true; return; }
  const p = track.points[best];
  const when = new Date((track.summary.start + p[4]) * 1000).toLocaleTimeString();
  tip.innerHTML = `<b>${(p[2] * units.speedF).toFixed(1)} ${units.speed}</b><br>${(p[3] * units.altF).toFixed(0)} ${units.alt} | ${when}`;
  tip.style.left = e.clientX + 14 + 'px';
  tip.style.top = e.clientY + 14 + 'px';
  tip.hidden = false;
});
$('#trackMap').addEventListener('mouseleave', () => { $('#mapTip').hidden = true; });

// ------------------------------------------------------------------ settings

async function loadSettings() {
  try {
    settingsData = await api('/api/settings');
    pending = {};
    renderSettings();
  } catch (e) {
    $('#settingsForm').innerHTML = `<div class="card empty">Could not load settings: ${esc(e.message)}</div>`;
  }
}

function controlHtml(it) {
  const id = 's_' + it.key;
  switch (it.type) {
    case 'bool':
      return `<label class="switch"><input type="checkbox" id="${id}"${it.value ? ' checked' : ''}><span></span></label>`;
    case 'choice':
      return `<select id="${id}">${it.options.map((o, i) => `<option value="${i}"${i === it.value ? ' selected' : ''}>${esc(o)}</option>`).join('')}</select>`;
    case 'int':
      return `<div class="range"><input type="range" id="${id}" min="${it.min}" max="${it.max}" step="${it.step}" value="${it.value}"><output>${intText(it, it.value)}</output></div>`;
    default: {
      const placeholder = it.secret ? (it.isSet ? 'unchanged' : 'not set') : '';
      return `<input type="${it.secret ? 'password' : 'text'}" id="${id}" maxlength="${it.maxLen}" value="${esc(it.value)}" placeholder="${placeholder}" autocomplete="off" spellcheck="false">`;
    }
  }
}

function intText(it, raw) {
  return `${(raw / 10 ** it.dec).toFixed(it.dec)}${it.unit ? (it.unit === '%' ? '%' : ' ' + it.unit) : ''}`;
}

function renderSettings() {
  const form = $('#settingsForm');
  form.innerHTML = '';
  settingsData.groups.forEach((groupName, groupIndex) => {
    const card = document.createElement('div');
    card.className = 'card';
    card.innerHTML = `<h3>${esc(groupName)}</h3>`;
    settingsData.items.filter(it => it.group === groupIndex).forEach(it => {
      const row = document.createElement('div');
      row.className = 'setting';
      row.dataset.key = it.key;
      row.dataset.search = `${it.label} ${it.help} ${it.key} ${groupName}`.toLowerCase();
      row.innerHTML = `<div class="s-info"><label for="s_${it.key}">${esc(it.label)}</label><small>${esc(it.help)}</small></div><div class="s-ctl">${controlHtml(it)}</div>`;
      const input = row.querySelector('input, select');
      const handler = () => onSettingInput(it, input, row);
      input.addEventListener('input', handler);
      input.addEventListener('change', handler);
      card.appendChild(row);
    });
    form.appendChild(card);
  });
  applyFilter();
  updateSaveBar();
}

function onSettingInput(it, input, row) {
  let value;
  if (it.type === 'bool') value = input.checked;
  else if (it.type === 'choice' || it.type === 'int') value = Number(input.value);
  else value = input.value;
  if (it.type === 'int') row.querySelector('output').textContent = intText(it, value);
  const original = it.secret ? '' : it.value;
  if (value === original) delete pending[it.key];
  else pending[it.key] = value;
  row.classList.toggle('dirty', it.key in pending);
  row.classList.remove('error');
  updateSaveBar();
}

function updateSaveBar() {
  const count = Object.keys(pending).length;
  $('#saveBar').hidden = count === 0;
  setText('saveInfo', `${count} unsaved change${count === 1 ? '' : 's'}`);
}

function applyFilter() {
  const q = $('#settingsFilter').value.trim().toLowerCase();
  $$('#settingsForm .card').forEach(card => {
    let visible = 0;
    $$('.setting', card).forEach(row => {
      const show = !q || row.dataset.search.includes(q);
      row.hidden = !show;
      if (show) visible++;
    });
    card.hidden = visible === 0;
  });
}

async function saveSettings(values) {
  const keys = Object.keys(values);
  if (!keys.length) return;
  const wifiChange = keys.some(k => WIFI_KEYS.includes(k));
  try {
    const result = await post('/api/settings', values);
    const errors = result.errors || {};
    const errorKeys = Object.keys(errors);
    if (errorKeys.length) {
      toast(`Saved ${result.changed}, ${errorKeys.length} rejected: ${errorKeys.map(k => `${k} (${errors[k]})`).join(', ')}`, true);
    } else {
      toast(wifiChange ? 'Saved. WiFi is restarting - reconnect if this page stops updating.' : `Saved ${result.changed} setting${result.changed === 1 ? '' : 's'}`);
    }
    const kept = {};
    errorKeys.forEach(k => { kept[k] = values[k]; });
    await loadSettings();
    pending = kept;
    errorKeys.forEach(k => { const row = $(`.setting[data-key="${k}"]`); if (row) row.classList.add('error'); });
    updateSaveBar();
  } catch (e) {
    toast('Save failed: ' + e.message, true);
  }
}

$('#settingsFilter').addEventListener('input', applyFilter);
$('#btnSave').addEventListener('click', () => saveSettings({...pending}));
$('#btnRevert').addEventListener('click', () => { pending = {}; renderSettings(); });

$('#btnBackup').addEventListener('click', () => {
  if (!settingsData) return;
  const backup = {};
  settingsData.items.filter(it => !it.secret).forEach(it => { backup[it.key] = it.value; });
  const blob = new Blob([JSON.stringify(backup, null, 2)], {type: 'application/json'});
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = `${(status && status.name || 'gps-logger').replace(/\W+/g, '-')}-settings.json`;
  document.body.appendChild(a);
  a.click();
  setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
});

$('#fileRestore').addEventListener('change', async e => {
  const file = e.target.files[0];
  e.target.value = '';
  if (!file) return;
  try {
    const values = JSON.parse(await file.text());
    if (typeof values !== 'object' || Array.isArray(values)) throw new Error('not a settings object');
    if (!confirm(`Restore ${Object.keys(values).length} settings from ${file.name}?`)) return;
    await saveSettings(values);
  } catch (err) {
    toast('Restore failed: ' + err.message, true);
  }
});

// ------------------------------------------------------------------ firmware update

$('#btnFlash').addEventListener('click', () => {
  const file = $('#fwFile').files[0];
  if (!file) { toast('Choose a firmware.bin first', true); return; }
  if (!confirm(`Flash ${file.name} (${fmt.bytes(file.size)})? The logger restarts when done.`)) return;
  const form = new FormData();
  form.append('firmware', file, file.name);
  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/api/update');
  xhr.upload.addEventListener('progress', ev => {
    if (!ev.lengthComputable) return;
    const pct = ev.loaded * 100 / ev.total;
    $('#fwBar').style.width = pct + '%';
    setText('fwStatus', `Uploading ${pct.toFixed(0)}%`);
  });
  xhr.addEventListener('load', () => {
    const ok = xhr.status === 200;
    setText('fwStatus', ok ? 'Update complete, restarting...' : `Update failed: ${xhr.responseText}`);
    toast(ok ? 'Firmware updated' : 'Update failed', !ok);
  });
  xhr.addEventListener('error', () => { setText('fwStatus', 'Upload error'); toast('Upload error', true); });
  $('#btnFlash').disabled = true;
  xhr.addEventListener('loadend', () => { $('#btnFlash').disabled = false; });
  xhr.send(form);
});

// ------------------------------------------------------------------ boot

let resizeTimer = 0;
window.addEventListener('resize', () => {
  clearTimeout(resizeTimer);
  resizeTimer = setTimeout(() => { renderCharts(); if (track && activeTab === 'sessions') { drawTrack(); drawProfile(); } }, 150);
});
matchMedia('(prefers-color-scheme: light)').addEventListener('change', () => { renderCharts(); if (track) { drawTrack(); drawProfile(); } });

setInterval(() => { if (activeTab === 'dash' && !document.hidden) loadLcd(); }, 800);

const initialTab = location.hash.slice(1);
showTab(['dash', 'sessions', 'settings', 'system'].includes(initialTab) ? initialTab : 'dash');
pollStatus();
</script>
</body>
</html>
)==";
