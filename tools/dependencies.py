#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Fetch the pinned public dependencies into ignored build storage.

JUCE is pinned to the revision sample-base uses, so Melodee's drawing code
builds against the same juce::Graphics here as on the MPC Sample."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
JUCE_TAG = "9.0.5"
JUCE_REVISION = "d60b7a78030661574a615b586d9325ba7a7fa7dd"


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
