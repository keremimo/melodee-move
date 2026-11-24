#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Drives Melodee's web page on a simulated run (./build.sh), as two browsers:

- one on the Move itself (127.0.0.1), which the page lets in at once, as it
  does a browser on the USB cable: the page, the status, the screen's
  stream, and Melodee's folder (a folder made, a sample uploaded, read back,
  renamed, deleted);
- one at the machine's own network address, which the page treats as on
  Wi-Fi: refused until it pairs. It asks for a code, the code is read off
  the Move's screen through the first browser's stream (the 5 x 7 digits of
  src/move/font.cpp, as a person reads them), typed, and then it may upload.

Usage: web_check.py PORT FONT_CPP
"""
import base64
import http.client
import json
import re
import socket
import sys
import time

PORT = int(sys.argv[1])
GLYPHS = {}
for match in re.finditer(r"\{(0x\w\w), (0x\w\w), (0x\w\w), (0x\w\w), (0x\w\w)\}, // (\d)\s*$", open(sys.argv[2]).read(), re.M):
    GLYPHS[tuple(int(x, 16) for x in match.groups()[:5])] = match.group(6)
failures = []


def check(ok, what):
    if not ok:
        failures.append(what)
        print("web check: FAILED: " + what, file=sys.stderr)


def call(host, method, path, body=b"", headers=None):
    connection = http.client.HTTPConnection(host, PORT, timeout=5)
    all_headers = {"X-Melodee": "1"} if method == "POST" else {}
    all_headers.update(headers or {})
    connection.request(method, path, body=body if method == "POST" else None, headers=all_headers)
    reply = connection.getresponse()
    data = reply.read()
    connection.close()
    return reply.status, data, reply.getheader("Set-Cookie")


def frames(host, count, wanted=None, limit=6.0, headers=None):
    """Frames from the screen's stream, as 1024 bytes each; with `wanted`, until one it likes."""
    connection = http.client.HTTPConnection(host, PORT, timeout=limit)
    connection.request("GET", "/api/screen", headers=headers or {})
    reply = connection.getresponse()
    if reply.status != 200:
        return []
    out, ends = [], time.time() + limit
    while time.time() < ends and len(out) < count:
        line = reply.fp.readline()
        if not line:
            break
        if line.startswith(b"data: "):
            frame = base64.b64decode(line[6:].strip())
            if wanted is None or wanted(frame):
                out.append(frame)
    connection.close()
    return out


def lit(frame, x, y):
    return bool(frame[(y >> 3) * 128 + x] & (1 << (y & 7)))


def code_on(frame):
    """The six digits of the pairing screen (web/screens.cpp: "482 913" at scale 2, centred at y 16)."""
    text_width = 7 * 6 * 2 - 2
    left, digits = 26 + (102 - text_width) // 2, ""
    for n in range(7):
        if n == 3:
            continue
        columns = []
        for c in range(5):
            bits = 0
            for r in range(7):
                if lit(frame, left + n * 12 + c * 2, 16 + r * 2):
                    bits |= 1 << r
            columns.append(bits)
        digit = GLYPHS.get(tuple(columns))
        if digit is None:
            return None
        digits += digit
    return digits


# The run is starting: wait for the page.
for _ in range(50):
    try:
        status, page, _ = call("127.0.0.1", "GET", "/")
        break
    except OSError:
        time.sleep(0.1)
else:
    sys.exit("web check: the page never answered on port %d" % PORT)
check(status == 200 and b"Melodee" in page and b"Saving a kit" in page, "the page and its guide")

# On the Move itself: open.
status, data, _ = call("127.0.0.1", "GET", "/api/status")
facts = json.loads(data)
check(status == 200 and facts["via"] == "local" and facts["version"].startswith("v"), "status from the Move: %s" % data)
shown = frames("127.0.0.1", 1)
check(len(shown) == 1 and sum(bin(b).count("1") for b in shown[0]) > 200, "the screen's stream shows Melodee's screen")

check(call("127.0.0.1", "POST", "/api/mkdir?path=&name=From%20the%20web")[0] == 200, "a folder made") # Samples/ is Sampling's, made at start
sample = b"RIFF" + bytes(range(256)) * 40
check(call("127.0.0.1", "POST", "/api/upload?path=From%20the%20web&name=Web%20Kick.wav", sample)[0] == 200, "a sample uploaded")
status, data, _ = call("127.0.0.1", "GET", "/api/list?path=From%20the%20web")
check(status == 200 and any(e["name"] == "Web Kick.wav" and e["size"] == len(sample) for e in json.loads(data)["entries"]), "listed: %s" % data)
check(call("127.0.0.1", "GET", "/api/file?path=From%20the%20web/Web%20Kick.wav")[1] == sample, "read back whole")
check(call("127.0.0.1", "POST", "/api/rename?path=From%20the%20web/Web%20Kick.wav&to=Kick.wav")[0] == 200, "renamed")
check(call("127.0.0.1", "POST", "/api/delete?path=From%20the%20web/Kick.wav")[0] == 200, "deleted")
check(call("127.0.0.1", "POST", "/api/delete?path=")[0] == 403, "Melodee's folder itself stays")

# At this machine's own address: as on Wi-Fi.
probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
probe.connect(("10.255.255.255", 1))
wifi = probe.getsockname()[0]
probe.close()
check(not wifi.startswith("127."), "an address other than loopback: %s" % wifi)
check(call(wifi, "GET", "/")[0] == 200, "the page itself from Wi-Fi")
check(call(wifi, "GET", "/api/status")[0] == 401, "status refused before pairing")
check(call(wifi, "GET", "/api/list?path=")[0] == 401, "files refused before pairing")
check(call(wifi, "POST", "/api/pair?code=000000")[0] == 410, "no code to try before asking")
check(call(wifi, "POST", "/api/pair/ask")[0] == 200, "a code asked for")
pairing = frames("127.0.0.1", 1, lambda f: code_on(f) is not None)
code = code_on(pairing[0]) if pairing else None
check(code is not None, "the code read off the Move's screen")
if code:
    wrong = "%06d" % ((int(code) + 1) % 1000000)
    status, data, _ = call(wifi, "POST", "/api/pair?code=" + wrong)
    check(status == 403 and json.loads(data)["left"] == 4, "a wrong code: %s" % data)
    status, _, cookie = call(wifi, "POST", "/api/pair?code=" + code)
    check(status == 200 and cookie and "HttpOnly" in cookie, "paired with the code from the screen")
    jar = {"Cookie": cookie.split(";")[0]} if cookie else {}
    status, data, _ = call(wifi, "GET", "/api/status", headers=jar)
    check(status == 200 and json.loads(data)["via"] == "wifi", "status once paired: %s" % data)
    check(call(wifi, "POST", "/api/upload?path=Samples&name=From%20Wi-Fi.wav", sample, jar)[0] == 200, "an upload once paired")
    check(len(frames(wifi, 1, headers=jar)) == 1, "the screen's stream once paired")
check(frames(wifi, 1) == [], "no screen for a browser that has not paired")

if failures:
    sys.exit("web check: %d failed" % len(failures))
print("web check: ok (page, status, screen stream, files; Wi-Fi at %s refused, paired with the code %s read off the screen, then let in)"
      % (wifi, code))
