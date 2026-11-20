// SPDX-License-Identifier: AGPL-3.0-only
// Melodee's web page on the Move: the screen mirrored, Melodee's folder, the
// guide; and, for a browser on Wi-Fi, pairing with a code the Move shows.
// The server is src/web/web_page.cpp; every change it is sent carries an
// X-Melodee header, which a page on another site cannot add.
'use strict';
const $ = (id) => document.getElementById(id);
const views = ['screen', 'files', 'guide', 'pair'];
const paths = {screen: '/', files: '/files', guide: '/guide'};
let current = null, paired = true, statusTimer = 0;

function call(path, options = {}) {
  return fetch(path, {...options, credentials: 'same-origin', headers: {'X-Melodee': '1', ...(options.headers || {})}});
}
async function json(path, options) {
  const reply = await call(path, options);
  let body = {};
  try { body = await reply.json(); } catch (e) { /* an empty reply */ }
  if (reply.status === 401) { needPairing(); throw new Error(body.error || 'Pair this browser first'); }
  if (!reply.ok) throw Object.assign(new Error(body.error || reply.statusText), {status: reply.status, body});
  return body;
}
const enc = encodeURIComponent;

function bytes(n) {
  if (n < 1024) return n + ' B';
  const units = ['KB', 'MB', 'GB', 'TB'];
  let u = -1;
  do { n /= 1024; ++u; } while (n >= 1024 && u < units.length - 1);
  return (n >= 100 ? n.toFixed(0) : n.toFixed(1)) + ' ' + units[u];
}
function when(seconds) {
  const d = new Date(seconds * 1000), now = new Date();
  const same = d.toDateString() === now.toDateString();
  return same ? d.toLocaleTimeString([], {hour: '2-digit', minute: '2-digit'})
              : d.toLocaleDateString([], {day: 'numeric', month: 'short', year: d.getFullYear() === now.getFullYear() ? undefined : 'numeric'});
}

// ---- Views ----

function show(view, push) {
  if (!paired && view !== 'pair') view = 'pair';
  current = view;
  for (const v of views) $('view-' + v).hidden = v !== view;
  for (const a of document.querySelectorAll('#tabs a')) {
    if (a.dataset.view === view) a.setAttribute('aria-current', 'page'); else a.removeAttribute('aria-current');
  }
  $('tabs').classList.toggle('locked', !paired);
  if (push && paths[view] && !window.MELODEE_MOCK) history.pushState({view}, '', paths[view]);
  if (view === 'screen') mirror.open(); else mirror.close();
  if (view === 'files') files.list(files.path);
}
function viewOfPath() {
  const p = location.pathname.replace(/\/+$/, '') || '/';
  return Object.keys(paths).find((v) => paths[v] === p) || 'screen';
}
document.addEventListener('click', (e) => {
  const a = e.target.closest('a[data-view]');
  if (!a || e.metaKey || e.ctrlKey || e.shiftKey) return;
  e.preventDefault();
  show(a.dataset.view, true);
});
window.addEventListener('popstate', () => show(viewOfPath(), false));

// ---- The link to the Move, and the cards ----

function link(state, word) {
  $('link').dataset.state = state;
  $('link-word').textContent = word;
}
async function status() {
  clearTimeout(statusTimer);
  try {
    const s = await json('/api/status');
    paired = true;
    link('on', s.via === 'wifi' ? 'Wi-Fi' : s.via === 'usb' ? 'USB' : 'Move');
    $('v-version').textContent = s.version;
    $('v-free').innerHTML = bytes(s.free) + ' <small>free</small>';
    $('g-free').style.width = s.total ? (100 * (1 - s.free / s.total)).toFixed(1) + '%' : '0';
    $('v-via').textContent = s.via === 'wifi' ? 'Wi-Fi, paired' : s.via === 'usb' ? 'USB cable' : 'On the Move';
    $('v-where').textContent = location.host;
    if (current === 'pair' || current === null) show(viewOfPath(), false);
  } catch (e) {
    if (paired) link('off', 'Move');
  }
  statusTimer = setTimeout(status, 3000);
}

// ---- Screen: the Move's 128 x 64 one-bit screen, live ----

const mirror = (() => {
  const canvas = $('mirror'), g = canvas.getContext('2d'), image = g.createImageData(128, 64);
  const lit = [0xd8, 0xf0, 0xff], dark = [0x05, 0x06, 0x08];
  let source = null, frame = null, count = 0, rateTimer = 0;
  function live(on, word) {
    $('live').dataset.state = on ? 'on' : 'off';
    $('live-word').textContent = word;
    $('mirror-veil').hidden = on;
    if (!on) $('mirror-veil').firstChild.textContent = word === 'Off' ? 'Waiting for the Move' : word;
  }
  function draw(bits) {
    frame = bits;
    for (let y = 0; y < 64; ++y)
      for (let x = 0; x < 128; ++x) {
        const on = bits[(y >> 3) * 128 + x] & (1 << (y & 7)), c = on ? lit : dark, i = (y * 128 + x) * 4;
        image.data[i] = c[0]; image.data[i + 1] = c[1]; image.data[i + 2] = c[2]; image.data[i + 3] = 255;
      }
    g.putImageData(image, 0, 0);
  }
  function open() {
    if (source) return;
    live(false, 'Connecting');
    source = new EventSource('/api/screen');
    source.onmessage = (e) => {
      const raw = atob(e.data), bits = new Uint8Array(raw.length);
      for (let i = 0; i < raw.length; ++i) bits[i] = raw.charCodeAt(i);
      if (bits.length === 1024) { draw(bits); ++count; live(true, 'Live'); }
    };
    source.onerror = () => live(false, 'Lost the Move, trying again');
    count = 0;
    rateTimer = setInterval(() => { $('rate').textContent = count ? count + (count === 1 ? ' frame' : ' frames') + ' a second' : 'unchanged'; count = 0; }, 1000);
  }
  function close() {
    if (source) source.close();
    source = null;
    clearInterval(rateTimer);
    $('rate').textContent = '';
    live(false, 'Off');
  }
  $('snap').addEventListener('click', () => {
    if (!frame) return;
    const big = document.createElement('canvas'), s = 6;
    big.width = 128 * s; big.height = 64 * s;
    const b = big.getContext('2d');
    b.fillStyle = '#050608'; b.fillRect(0, 0, big.width, big.height);
    b.fillStyle = '#d8f0ff';
    for (let y = 0; y < 64; ++y)
      for (let x = 0; x < 128; ++x)
        if (frame[(y >> 3) * 128 + x] & (1 << (y & 7))) b.fillRect(x * s, y * s, s, s);
    big.toBlob((blob) => {
      const a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = 'Move screen ' + new Date().toISOString().slice(0, 19).replace('T', ' ').replace(/:/g, '.') + '.png';
      a.click();
      setTimeout(() => URL.revokeObjectURL(a.href), 1000);
    });
  });
  return {open, close};
})();

// ---- Files: Melodee's folder on the Move ----

const icons = {
  dir: '<svg class="icon" viewBox="0 0 20 20"><path d="M2.5 5h5.5l1.5 2h8v9h-15z" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linejoin="round"/></svg>',
  audio: '<svg class="icon" viewBox="0 0 20 20"><path d="M3 10h2l1.5-4 3 9 2.5-11 2 9 1.5-3H17" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linejoin="round" stroke-linecap="round"/></svg>',
  file: '<svg class="icon" viewBox="0 0 20 20"><path d="M5 2.5h6.5l3.5 3.5v11.5H5z M11.5 2.5V6H15" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linejoin="round"/></svg>',
  play: '<svg viewBox="0 0 16 16"><path d="M4.5 3v10l8-5z" fill="currentColor"/></svg>',
  stop: '<svg viewBox="0 0 16 16"><rect x="4" y="4" width="8" height="8" rx="1" fill="currentColor"/></svg>',
  save: '<svg viewBox="0 0 16 16"><path d="M8 2.5v8 M4.5 7 8 10.5 11.5 7 M3 13.5h10" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"/></svg>',
  rename: '<svg viewBox="0 0 16 16"><path d="M10.5 2.5l3 3-8 8H2.5v-3z" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linejoin="round"/></svg>',
  del: '<svg viewBox="0 0 16 16"><path d="M3 4.5h10 M6.5 4.5V3h3v1.5 M4.5 4.5l.7 9h5.6l.7-9" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/></svg>',
};
const audio = /\.(wav|aif|aiff|flac|ogg|mp3)$/i;
const esc = (s) => s.replace(/[&<>"]/g, (c) => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;'}[c]));

const files = (() => {
  const api = {path: ''};
  const list = $('entries'), player = $('player');
  let playing = null, fresh = null;
  const join = (name) => (api.path ? api.path + '/' : '') + name;

  function crumbs() {
    const parts = api.path ? api.path.split('/') : [];
    let html = '<a data-path="">Melodee</a>';
    parts.forEach((part, n) => { html += '<span class="sep">/</span><a data-path="' + esc(parts.slice(0, n + 1).join('/')) + '">' + esc(part) + '</a>'; });
    $('crumbs').innerHTML = html;
  }
  api.list = async (path) => {
    api.path = path;
    crumbs();
    let folder;
    try { folder = await json('/api/list?path=' + enc(path)); } catch (e) {
      list.innerHTML = '<li class="empty">' + esc(e.message) + '</li>';
      return;
    }
    $('files-foot').textContent = folder.home + (api.path ? '/' + api.path : '') + ' · ' + bytes(folder.free) + ' free on the Move';
    if (!folder.entries.length) {
      list.innerHTML = '<li class="empty">' + (api.path ? 'Empty. Drop files here to upload them.' : 'Nothing here yet. Upload samples, or make a folder for them.') + '</li>';
      return;
    }
    list.innerHTML = folder.entries.map((e) => {
      const kind = e.dir ? 'dir' : audio.test(e.name) ? 'audio' : 'file';
      const stripe = kind === 'dir' ? 'var(--lilac)' : kind === 'audio' ? 'var(--coral)' : 'var(--amber)';
      const rel = esc(join(e.name));
      return '<li class="entry ' + kind + (e.name === fresh ? ' fresh' : '') + '" style="--stripe:' + stripe + '" data-name="' + esc(e.name) + '" data-path="' + rel + '">'
        + icons[kind] + '<span class="name">' + esc(e.name) + '</span>'
        + '<span class="meta size">' + (e.dir ? '' : bytes(e.size)) + '</span><span class="meta when">' + when(e.time) + '</span>'
        + '<span class="acts">'
        + (kind === 'audio' ? '<button class="act play" title="Play" data-act="play">' + icons.play + '</button>' : '')
        + (e.dir ? '' : '<a class="act" title="Download" href="/api/file?path=' + enc(join(e.name)) + '&save=1">' + icons.save + '</a>')
        + '<button class="act" title="Rename" data-act="rename">' + icons.rename + '</button>'
        + '<button class="act del" title="Delete" data-act="delete">' + icons.del + '</button>'
        + '</span></li>';
    }).join('');
    fresh = null;
  };

  $('crumbs').addEventListener('click', (e) => {
    const a = e.target.closest('a[data-path]');
    if (a) api.list(a.dataset.path);
  });
  list.addEventListener('click', async (e) => {
    const row = e.target.closest('.entry');
    if (!row) return;
    const act = e.target.closest('[data-act]');
    if (!act) {
      if (row.classList.contains('dir') && !row.querySelector('.rename')) api.list(row.dataset.path);
      return;
    }
    if (act.dataset.act === 'play') {
      const again = playing === row.dataset.path;
      player.pause();
      for (const r of list.querySelectorAll('.playing')) { r.classList.remove('playing'); r.querySelector('.play').innerHTML = icons.play; }
      playing = null;
      if (again) return;
      playing = row.dataset.path;
      player.src = '/api/file?path=' + enc(row.dataset.path);
      player.play().catch(() => {});
      row.classList.add('playing');
      act.innerHTML = icons.stop;
    } else if (act.dataset.act === 'delete') {
      const what = row.classList.contains('dir') ? 'the folder “' + row.dataset.name + '” and everything in it' : '“' + row.dataset.name + '”';
      if (!confirm('Delete ' + what + ' from the Move? It cannot be undone.')) return;
      try { await json('/api/delete?path=' + enc(row.dataset.path), {method: 'POST'}); } catch (err) { alert(err.message); }
      api.list(api.path);
    } else if (act.dataset.act === 'rename') {
      rename(row);
    }
  });
  player.addEventListener('ended', () => api.list(api.path));

  function rename(row) {
    const name = row.querySelector('.name'), old = row.dataset.name;
    const input = document.createElement('input');
    input.className = 'rename';
    input.value = old;
    name.replaceWith(input);
    input.focus();
    const dot = old.lastIndexOf('.');
    input.setSelectionRange(0, dot > 0 && !row.classList.contains('dir') ? dot : old.length);
    let done = false;
    const finish = async (keep) => {
      if (done) return;
      done = true;
      const to = input.value.trim();
      if (keep && to && to !== old) {
        try {
          await json('/api/rename?path=' + enc(row.dataset.path) + '&to=' + enc(to), {method: 'POST'});
          fresh = to;
        } catch (err) { alert(err.message); }
      }
      api.list(api.path);
    };
    input.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') finish(true);
      if (e.key === 'Escape') finish(false);
    });
    input.addEventListener('blur', () => finish(true));
  }

  $('new-folder').addEventListener('click', async () => {
    const name = prompt('New folder in ' + (api.path || 'Melodee') + ':', 'Samples');
    if (!name) return;
    try {
      await json('/api/mkdir?path=' + enc(api.path) + '&name=' + enc(name.trim()), {method: 'POST'});
      fresh = name.trim();
    } catch (err) { alert(err.message); }
    api.list(api.path);
  });

  // Uploads, one at a time, by button or dropped on the list.
  const queue = [];
  let sending = false;
  function upload(list) {
    for (const file of list) queue.push({file, path: api.path});
    if (!sending) next();
  }
  function next() {
    const job = queue.shift();
    if (!job) { sending = false; $('queue').hidden = true; return; }
    sending = true;
    $('queue').hidden = false;
    $('queue-words').textContent = 'Sending ' + job.file.name + (queue.length ? ' (' + queue.length + ' more)' : '');
    $('queue-bar').style.width = '0';
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/upload?path=' + enc(job.path) + '&name=' + enc(job.file.name));
    xhr.setRequestHeader('X-Melodee', '1');
    xhr.upload.onprogress = (e) => { if (e.lengthComputable) $('queue-bar').style.width = (100 * e.loaded / e.total).toFixed(1) + '%'; };
    xhr.onloadend = () => {
      let body = {};
      try { body = JSON.parse(xhr.responseText); } catch (e) { /* none */ }
      if (xhr.status === 200) fresh = body.name;
      else alert('Could not upload ' + job.file.name + ': ' + (body.error || 'the Move did not answer'));
      if (job.path === api.path) api.list(api.path);
      next();
    };
    xhr.send(job.file);
  }
  $('upload-input').addEventListener('change', (e) => { upload(e.target.files); e.target.value = ''; });
  const area = $('file-list');
  let depth = 0;
  area.addEventListener('dragenter', (e) => { if (e.dataTransfer.types.includes('Files')) { ++depth; area.classList.add('over'); e.preventDefault(); } });
  area.addEventListener('dragover', (e) => { if (e.dataTransfer.types.includes('Files')) e.preventDefault(); });
  area.addEventListener('dragleave', () => { if (--depth <= 0) { depth = 0; area.classList.remove('over'); } });
  area.addEventListener('drop', (e) => {
    e.preventDefault();
    depth = 0;
    area.classList.remove('over');
    upload(e.dataTransfer.files);
  });
  return api;
})();

// ---- Pairing: a browser on Wi-Fi types the code the Move shows ----

const pairing = (() => {
  const digits = [...document.querySelectorAll('#digits input')];
  let poll = 0, quietUntil = 0;
  function say(text, kind) { $('pair-say').textContent = text; $('pair-say').className = 'say' + (kind ? ' ' + kind : ''); }
  function code() { return digits.map((d) => d.value).join(''); }
  function clear() { for (const d of digits) d.value = ''; $('pair-send').disabled = true; digits[0].focus(); }
  function asking(on) {
    $('pair-form').hidden = !on;
    $('pair-ask').textContent = on ? 'Show a new code' : 'Show a code on the Move';
    clearInterval(poll);
    if (on) poll = setInterval(watch, 1500);
  }
  async function watch() {
    try {
      const s = await (await call('/api/pair')).json();
      if (s.state === 'waiting' && Date.now() > quietUntil) say('The Move shows a code for ' + s.seconds + ' more seconds.', 'ask');
      else if (s.state === 'refused') { asking(false); say('Refused on the Move. Ask again for a new code.', 'bad'); }
      else if (s.state === 'expired') { asking(false); say('The code ran out. Ask again for a new one.', 'bad'); }
    } catch (e) { /* the next try */ }
  }
  $('pair-ask').addEventListener('click', async () => {
    try {
      const s = await json('/api/pair/ask', {method: 'POST'});
      asking(true);
      clear();
      say('The Move shows a code for ' + s.seconds + ' seconds.', 'ask');
    } catch (e) { say(e.message, 'bad'); }
  });
  digits.forEach((d, n) => {
    d.addEventListener('input', () => {
      // One digit a box; several at once (typed fast, filled in) run on into the next boxes.
      const typed = d.value.replace(/\D/g, '').slice(0, 6 - n);
      d.value = '';
      typed.split('').forEach((c, i) => { digits[n + i].value = c; });
      if (typed) digits[Math.min(n + typed.length, 5)].focus();
      $('pair-send').disabled = code().length !== 6;
      if (code().length === 6) $('pair-form').requestSubmit();
    });
    d.addEventListener('keydown', (e) => { if (e.key === 'Backspace' && !d.value && n > 0) digits[n - 1].focus(); });
    d.addEventListener('paste', (e) => {
      const text = (e.clipboardData.getData('text') || '').replace(/\D/g, '').slice(0, 6);
      if (!text) return;
      e.preventDefault();
      text.split('').forEach((c, i) => { digits[i].value = c; });
      $('pair-send').disabled = code().length !== 6;
      if (code().length === 6) $('pair-form').requestSubmit();
    });
  });
  $('pair-form').addEventListener('submit', async (e) => {
    e.preventDefault();
    if (code().length !== 6) return;
    $('pair-send').disabled = true;
    const reply = await call('/api/pair?code=' + code(), {method: 'POST'});
    const body = await reply.json().catch(() => ({}));
    if (reply.ok) {
      asking(false);
      say('Paired. This browser may use the page until Melodee stops on the Move.', 'ok');
      paired = true;
      setTimeout(() => { show(viewOfPath(), false); status(); }, 700);
      return;
    }
    $('digits').classList.remove('wrong');
    void $('digits').offsetWidth;
    $('digits').classList.add('wrong');
    quietUntil = Date.now() + 4000; // the countdown waits while this is read
    if (reply.status === 403 && body.left > 0) { say('Wrong code. ' + body.left + (body.left === 1 ? ' try' : ' tries') + ' left.', 'bad'); clear(); }
    else { asking(false); say((body.error || 'Refused') + '. Ask again for a new code.', 'bad'); }
  });
  return {stop: () => asking(false)};
})();

function needPairing() {
  if (!paired && current === 'pair') return;
  paired = false;
  link('busy', 'Pair');
  show('pair', false);
}

show(viewOfPath(), false);
status();
