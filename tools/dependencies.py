#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Fetch the pinned public dependencies into ignored build storage.

JUCE is pinned to the revision sample-base uses, so Melodee's drawing code
builds against the same juce::Graphics here as on the MPC Sample.

QuickJS is the release Schwung builds (its libs/quickjs/quickjs-2025-04-26,
identical file for file), so a Schwung module's page script runs on the same
engine here as under Schwung."""
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
