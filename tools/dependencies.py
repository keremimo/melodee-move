#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Fetch the pinned public dependencies into ignored build storage.

JUCE is pinned to the revision sample-base uses, so Melodee's drawing code
builds against the same juce::Graphics here as on the MPC Sample.

QuickJS is the release Schwung builds (its libs/quickjs/quickjs-2025-04-26,
identical file for file), so a Schwung module's page script runs on the same
engine here as under Schwung.

Schwung's shared JavaScript (MIT) is the release the Move runs (v1.9.4), for
the tests only: a module's pages are driven by Schwung's own page controller
(src/app/schwung_pages.h), which on the Move is imported from the Schwung
installed there and is never shipped or committed here.

Rubik (SIL OFL 1.1) is the face of Melodee's web page, pinned to the
google/fonts revision melodee-sample embeds; the page carries it
(tools/web_bundle.py)."""
from pathlib import Path
import hashlib
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
JUCE_TAG = "9.0.5"
JUCE_REVISION = "d60b7a78030661574a615b586d9325ba7a7fa7dd"
QUICKJS_VERSION = "2025-04-26"
QUICKJS_URL = f"https://bellard.org/quickjs/quickjs-{QUICKJS_VERSION}.tar.xz"
QUICKJS_SHA256 = "2f20074c25166ef6f781f381c50d57b502cb85d470d639abccebbef7954c83bf"
SCHWUNG_REVISION = "b9815aeee21f873ffac68d568f7a919226d64d86"  # charlesvestal/schwung v1.9.4
SCHWUNG_URL = f"https://github.com/charlesvestal/schwung/archive/{SCHWUNG_REVISION}.tar.gz"
SCHWUNG_SHA256 = "8ecbb6fc9558d8da2e802bf244064ac54742cebfa32adc63fcbef915ef1cc2f1"
FONTS_REVISION = "5bb28733cec313b63fbd415b574b5ae35c8aa8b8"  # google/fonts, as melodee-sample
RUBIK = {
    "Rubik-VF.ttf": ("Rubik%5Bwght%5D.ttf", "1b3a7437ba2af80e465e773ed60c5036d1ba6ace492d89046dbcf18fb31e4e88"),
    "OFL.txt": ("OFL.txt", "472cbe7c25441df63e9c7864b43eb3c0f4b3df950c66a76224e6cfe1eae843fb"),
}


def prepare_rubik(root=ROOT):
    folder = Path(root) / "build/deps/rubik"
    folder.mkdir(parents=True, exist_ok=True)
    for name, (remote, digest) in RUBIK.items():
        path = folder / name
        if path.exists() and hashlib.sha256(path.read_bytes()).hexdigest() == digest:
            continue
        url = f"https://raw.githubusercontent.com/google/fonts/{FONTS_REVISION}/ofl/rubik/{remote}"
        with urllib.request.urlopen(url, timeout=60) as response:
            data = response.read()
        if hashlib.sha256(data).hexdigest() != digest:
            raise RuntimeError(f"Rubik {name} hash mismatch")
        path.write_bytes(data)


def prepare_quickjs(root=ROOT):
    path = Path(root) / "build/deps/quickjs"
    if (path / "quickjs.c").exists():
        return
    archive = Path(root) / f"build/deps/quickjs-{QUICKJS_VERSION}.tar.xz"
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        urllib.request.urlretrieve(QUICKJS_URL, archive)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != QUICKJS_SHA256:
        archive.unlink()
        raise RuntimeError(f"QuickJS archive hash mismatch: {digest}")
    staging = Path(root) / "build/deps/quickjs-staging"
    with tarfile.open(archive) as tar:
        tar.extractall(staging, filter="data")
    (staging / f"quickjs-{QUICKJS_VERSION}").rename(path)
    staging.rmdir()


def prepare_schwung(root=ROOT):
    """Schwung's src/shared, alone, in build/deps/schwung/shared."""
    path = Path(root) / "build/deps/schwung"
    if (path / "shared/param_pages/page_controller.mjs").exists():
        return
    archive = Path(root) / f"build/deps/schwung-{SCHWUNG_REVISION[:7]}.tar.gz"
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        urllib.request.urlretrieve(SCHWUNG_URL, archive)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != SCHWUNG_SHA256:
        archive.unlink()
        raise RuntimeError(f"Schwung archive hash mismatch: {digest}")
    prefix = f"schwung-{SCHWUNG_REVISION}/src/shared/"
    staging = Path(root) / "build/deps/schwung-staging"
    with tarfile.open(archive) as tar:
        members = [m for m in tar.getmembers() if m.name.startswith(prefix)]
        tar.extractall(staging, members=members, filter="data")
    path.mkdir(parents=True, exist_ok=True)
    (staging / prefix).rename(path / "shared")
    subprocess.run(["rm", "-rf", str(staging)], check=True)


def prepare(root=ROOT):
    path = Path(root) / "build/deps/JUCE"
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["git", "clone", "--depth", "1", "--branch", JUCE_TAG,
                        "https://github.com/juce-framework/JUCE.git", str(path)], check=True)
    revision = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
    if revision != JUCE_REVISION:
        raise RuntimeError("JUCE revision mismatch; remove build/deps/JUCE and rebuild")


if __name__ == "__main__":
    prepare()
    prepare_quickjs()
    prepare_schwung()
    prepare_rubik()
