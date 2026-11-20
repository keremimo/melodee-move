#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Builds Melodee's web page for the Move (src/web/web_bundle.h).

The page is src/web/static/: index.html, served at /, /files and /guide,
with the guide (docs/GUIDE.md, turned into HTML here) in it, the style
sheet, the script, Rubik (build/deps/rubik) and Melodee's wordmark, drawn by
melodee-sample's own tools/web_bundle.py from its docs/images (the
checkout in MELODEE, read only).

    web_bundle.py OUT.bin                  the bundle the binary carries
    web_bundle.py --mockup OUT.html [PNGS] the page in one file, on made-up
                                           data, its screen playing the UI
                                           kit's mockups in PNGS (128 x 64
                                           at any whole scale)

The bundle: "MVWB", then for each file its path, its type and its bytes,
each as a 32-bit little-endian length and that many bytes.
"""
import base64
import html
import json
import os
import re
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STATIC = ROOT / "src/web/static"
GUIDE = ROOT / "docs/GUIDE.md"
RUBIK = ROOT / "build/deps/rubik/Rubik-VF.ttf"
TYPES = {".html": "text/html; charset=utf-8", ".css": "text/css; charset=utf-8",
         ".js": "text/javascript; charset=utf-8", ".png": "image/png", ".ttf": "font/ttf"}


def slug(text):
    text = re.sub(r"<[^>]+>", "", text).strip().lower()
    return re.sub(r"[^\w\- ]", "", text).replace(" ", "-")


def inline(text):
    """Bold, code and links; the rest escaped."""
    codes = []

    def keep(match):
        codes.append("<code>" + html.escape(match.group(1)) + "</code>")
        return "\x00%d\x00" % (len(codes) - 1)

    text = re.sub(r"`([^`]+)`", keep, text)
    text = html.escape(text, quote=False)
    text = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", text)
    text = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)", lambda m: '<a href="%s">%s</a>' % (html.escape(m.group(2)), m.group(1)), text)
    return re.sub("\x00(\\d+)\x00", lambda m: codes[int(m.group(1))], text)


def guide_html(markdown):
    """docs/GUIDE.md as HTML, and its sections for the side list. It uses
    headings, paragraphs, lists of one level, bold, code and links."""
    out, toc, para, items = [], [], [], []

    def flush():
        if para:
            out.append("<p>%s</p>" % inline(" ".join(para)))
            para.clear()
        if items:
            out.append("<ul>%s</ul>" % "".join("<li>%s</li>" % inline(" ".join(i)) for i in items))
            items.clear()

    for line in markdown.splitlines():
        stripped = line.strip()
        heading = re.match(r"(#{1,3}) (.+)", stripped)
        if heading:
            flush()
            level, text = len(heading.group(1)), heading.group(2)
            anchor = slug(text)
            if level == 2:
                toc.append((text, anchor))
            out.append('<h%d id="%s">%s</h%d>' % (level, anchor, inline(text), level))
        elif stripped.startswith("- "):
            if para:
                flush()
            items.append([stripped[2:]])
        elif not stripped:
            flush()
        elif items and line.startswith("  "):
            items[-1].append(stripped)
        else:
            if items:
                flush()
            para.append(stripped)
    flush()
    side = '<nav class="toc" aria-label="In this guide"><p class="label">In this guide</p><ol>%s</ol></nav>' % "".join(
        '<li><a href="#%s">%s</a></li>' % (anchor, inline(text)) for text, anchor in toc)
    return "\n".join(out), side


def page():
    body, side = guide_html(GUIDE.read_text())
    return (STATIC / "index.html").read_text().replace("{{guide}}", body).replace("{{guide_toc}}", side)


def logo():
    """Melodee's wordmark as melodee-sample's page shows it, drawn by its own tool; None without the checkout."""
    melodee = Path(os.environ.get("MELODEE", ROOT.parent / "melodee-sample"))
    tool = melodee / "tools/web_bundle.py"
    if not tool.exists():
        return None
    sys.path.insert(0, str(tool.parent))
    sys.dont_write_bytecode = True  # nothing written into melodee-sample's checkout
    try:
        import web_bundle as sample  # melodee-sample's, run as it is
        return sample.logo(4)[0]
    except Exception as error:  # an older or newer checkout: the page shows the word in text instead
        print("web bundle: no wordmark (%s)" % error, file=sys.stderr)
        return None
    finally:
        sys.path.pop(0)


def files():
    out = {}
    index = page()
    for path in ("/", "/files", "/guide"):
        out[path] = (TYPES[".html"], index.encode())
    out["/app.css"] = (TYPES[".css"], (STATIC / "app.css").read_bytes())
    out["/app.js"] = (TYPES[".js"], (STATIC / "app.js").read_bytes())
    if RUBIK.exists():
        out["/font/rubik.ttf"] = (TYPES[".ttf"], RUBIK.read_bytes())
    mark = logo()
    if mark:
        out["/logo.png"] = (TYPES[".png"], mark)
    return out


def bundle(path):
    blob = bytearray(b"MVWB")
    for name, (kind, data) in files().items():
        for part in (name.encode(), kind.encode(), data):
            blob += struct.pack("<I", len(part)) + part
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_bytes(blob)
    print("web bundle: %d files, %.1f KB" % (len(files()), len(blob) / 1e3))


# ---- The mockup ----

def read_png(path):
    """An 8-bit RGB or RGBA PNG, as JUCE writes them: (width, height, rows of RGB(A) bytes)."""
    data = Path(path).read_bytes()
    at, idat, width = 8, b"", 0
    while at < len(data):
        length, kind = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + length]
        if kind == b"IHDR":
            width, height, depth, colour = struct.unpack(">IIBB", body[:10])
            if depth != 8 or colour not in (2, 6):
                raise ValueError("%s: not 8-bit RGB(A)" % path)
            channels = 3 if colour == 2 else 4
        elif kind == b"IDAT":
            idat += body
        at += 12 + length
    raw, stride, rows, prev = zlib.decompress(idat), width * channels, [], bytearray(width * channels)
    for y in range(height):
        kind, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b, c = prev[i], prev[i - channels] if i >= channels else 0
            if kind == 1: line[i] = (line[i] + a) & 255
            elif kind == 2: line[i] = (line[i] + b) & 255
            elif kind == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    return width, height, rows, channels


def frame_of(path):
    """A mockup PNG (the Move's screen at a whole scale) as the screen's 1024 bytes, base64."""
    width, height, rows, channels = read_png(path)
    scale = width // 128
    frame = bytearray(1024)
    for y in range(64):
        for x in range(128):
            row = rows[y * scale + scale // 2]
            if row[(x * scale + scale // 2) * channels] > 128:
                frame[(y >> 3) * 128 + x] |= 1 << (y & 7)
    return base64.b64encode(bytes(frame)).decode()


MOCK = r"""
// The mockup's Move: made-up files and answers, the screen playing the UI kit's mockups.
window.MELODEE_MOCK = true;
(() => {
  const now = Math.floor(Date.now() / 1000);
  const tree = {
    '': [{name: 'Projects', dir: true}, {name: 'Samples', dir: true}],
    'Projects': [{name: 'Move.mldp', size: 11873}, {name: 'Move.mldp.chops', size: 2210}, {name: 'Move.mldp.synth', size: 904}],
    'Samples': [{name: 'Breaks', dir: true}, {name: 'Vox', dir: true}, {name: 'Break 94.wav', size: 1486044}, {name: 'Dusty Keys.wav', size: 3897120},
                {name: 'Horn Stab.aif', size: 212400}, {name: 'Sub Drop.flac', size: 498210}, {name: 'notes.txt', size: 812}],
    'Samples/Breaks': [{name: 'Amen 136.wav', size: 2245110}, {name: 'Think 102.wav', size: 1701320}],
    'Samples/Vox': [],
  };
  for (const k in tree) tree[k].forEach((e, n) => { e.time = now - n * 5400 - (k.length * 86400); e.size = e.size || 0; e.dir = !!e.dir; });
  let mode = 'usb', asked = 0, tries = 5, pairedHere = false;
  const reply = (status, body) => new Response(JSON.stringify(body), {status, headers: {'Content-Type': 'application/json'}});
  const q = (url) => Object.fromEntries(new URL(url, 'http://move.local:8080').searchParams);
  window.fetch = async (url, options = {}) => {
    const path = url.split('?')[0], args = q(url), post = (options.method || 'GET') === 'POST';
    await new Promise((r) => setTimeout(r, 120));
    if (mode === 'gone') throw new TypeError('Failed to fetch');
    const locked = mode === 'wifi' && !pairedHere;
    if (path === '/api/pair/ask') { asked = Date.now(); tries = 5; return reply(200, {seconds: 90}); }
    if (path === '/api/pair' && !post) {
      const left = asked ? 90 - Math.floor((Date.now() - asked) / 1000) : 0;
      return reply(200, {state: asked ? (left > 0 ? 'waiting' : 'expired') : 'none', seconds: Math.max(0, left)});
    }
    if (path === '/api/pair') {
      if (args.code === '482913') { pairedHere = true; asked = 0; return reply(200, {}); }
      if (--tries > 0) return reply(403, {error: 'Wrong code', left: tries});
      asked = 0; return reply(410, {error: 'Five wrong codes: refused'});
    }
    if (locked) return reply(401, {error: 'Pair this browser first', pair: true});
    if (path === '/api/status') return reply(200, {version: 'v0.0.2', via: mode === 'wifi' ? 'wifi' : 'usb', free: 41.2e9, total: 50.8e9});
    if (path === '/api/list') return reply(200, {path: args.path, home: '/data/UserData/Melodee', free: 41.2e9,
      entries: [...(tree[args.path] || [])].sort((a, b) => (b.dir - a.dir) || a.name.localeCompare(b.name))});
    if (path === '/api/mkdir') { const dir = args.path; tree[dir].push({name: args.name, dir: true, size: 0, time: now}); tree[(dir ? dir + '/' : '') + args.name] = []; return reply(200, {name: args.name}); }
    if (path === '/api/delete' || path === '/api/rename') {
      const parts = args.path.split('/'), name = parts.pop(), dir = parts.join('/');
      const i = tree[dir].findIndex((e) => e.name === name);
      if (path === '/api/delete') tree[dir].splice(i, 1); else tree[dir][i].name = args.to;
      return reply(200, path === '/api/rename' ? {name: args.to} : {});
    }
    return reply(404, {error: 'Not found'});
  };
  const RealXHR = window.XMLHttpRequest;
  window.XMLHttpRequest = function () {
    const xhr = {upload: {}, status: 0, responseText: '', open(m, url) { this.url = url; }, setRequestHeader() {},
      send(file) {
        let sent = 0;
        const timer = setInterval(() => {
          sent = Math.min(file.size, sent + Math.max(file.size / 12, 1));
          if (this.upload.onprogress) this.upload.onprogress({lengthComputable: true, loaded: sent, total: file.size});
          if (sent >= file.size) {
            clearInterval(timer);
            const args = q(this.url);
            tree[args.path].push({name: args.name, size: file.size, time: Math.floor(Date.now() / 1000), dir: false});
            this.status = 200; this.responseText = JSON.stringify({name: args.name});
            this.onloadend();
          }
        }, 120);
      }};
    return xhr;
  };
  window.XMLHttpRequest.real = RealXHR;
  window.EventSource = function () {
    const source = {close() { clearInterval(timer); }};
    let n = 0;
    const timer = setInterval(() => {
      if (mode === 'gone' || (mode === 'wifi' && !pairedHere)) { if (source.onerror) source.onerror(); return; }
      if (source.onmessage) source.onmessage({data: FRAMES[n++ % FRAMES.length]});
    }, 1400);
    setTimeout(() => source.onmessage && mode !== 'gone' && source.onmessage({data: FRAMES[n++ % FRAMES.length]}), 300);
    return source;
  };
  document.addEventListener('DOMContentLoaded', () => {
    const bar = document.createElement('div');
    bar.className = 'mock-bar';
    bar.innerHTML = '<b>Mockup</b>' + [['usb', 'USB cable'], ['wifi', 'Wi-Fi, not paired'], ['gone', 'Move gone']].map(([m, t]) =>
      '<label><input type="radio" name="mock" value="' + m + '"' + (m === mode ? ' checked' : '') + '> ' + t + '</label>').join('') + '<span>Code: 482 913</span>';
    document.body.append(bar);
    bar.addEventListener('change', (e) => { mode = e.target.value; pairedHere = false; location.hash = mode; location.reload(); });
  });
  if (location.hash.slice(1) in {usb: 1, wifi: 1, gone: 1}) mode = location.hash.slice(1);
})();
"""

MOCK_CSS = """
.mock-bar { position: fixed; right: 16px; bottom: 16px; z-index: 50; display: flex; flex-wrap: wrap; gap: 12px; align-items: center; padding: 10px 14px;
  background: #2b2440; border: 1px solid #5a4d80; border-radius: 12px; font-size: 13px; color: #ddd3f5; box-shadow: 0 10px 30px rgba(0,0,0,.4); }
.mock-bar b { color: #FFC145; letter-spacing: .08em; text-transform: uppercase; font-size: 11px; }
.mock-bar span { color: #9A93A8; }
"""


def mockup(out, pngs):
    names = ["play", "chop", "touch", "load-samples", "module", "to-pads"]
    frames = [frame_of(Path(pngs) / (n + ".png")) for n in names if (Path(pngs) / (n + ".png")).exists()]
    if not frames:
        raise SystemExit("no mockup PNGs in %s (build/melodee-move --ui-mockups DIR makes them)" % pngs)
    index = page()
    css = (STATIC / "app.css").read_text()
    if RUBIK.exists():
        css = css.replace("url(/font/rubik.ttf)", "url(data:font/ttf;base64,%s)" % base64.b64encode(RUBIK.read_bytes()).decode())
    mark = logo()
    if mark:
        index = index.replace('src="/logo.png"', 'src="data:image/png;base64,%s"' % base64.b64encode(mark).decode())
    index = index.replace('<link rel="stylesheet" href="/app.css">', "<style>%s%s</style>" % (css, MOCK_CSS))
    script = "<script>const FRAMES = %s;%s</script><script>%s</script>" % (json.dumps(frames), MOCK, (STATIC / "app.js").read_text())
    index = index.replace('<script src="/app.js" defer></script>', script)
    Path(out).write_text(index)
    print("web mockup: %s (%d screens)" % (out, len(frames)))


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "--mockup":
        mockup(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else ROOT / "build/ui-mockups/rail")
    elif len(sys.argv) == 2:
        bundle(sys.argv[1])
    else:
        raise SystemExit(__doc__)
