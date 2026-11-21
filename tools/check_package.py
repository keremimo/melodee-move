#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Checks the asset Schwung's module catalog downloads, <id>-module.tar.gz,
as Schwung Manager takes it (Schwung's docs/MODULES.md, "Publishing to the
Module Catalog"; schwung-manager's installModule and boot_target.go): the
Manager unpacks it with the Move's BusyBox tar into modules/tools/, merging
file by file, and registers module.json's boot target.

- One folder, named for the module's id, holds everything, and nothing in
  it is a link, a device or outside it.
- module.json: the id, the version asked for, a standalone tool; its boot
  target's name 1-24 printable ASCII without a quote or backslash (what the
  selector's readers survive), its exec a file of the folder.
- standalone, boot-entry.sh and bin/* executable; every ELF file built for
  aarch64 (the Move's Cortex-A72).
- help.json: its topics under "children" (the only key the Help viewer
  reads), every line printable ASCII and at most 20 characters (a line is
  drawn on 128 pixels, never wrapped).
- Nothing the user owns (config.json, secrets/), no log, no macOS sidecar.
- Every library in lib/ beside its licence, licenses/lib/<library>.txt, and
  SOURCE.txt naming the source of this build.
- The privileged helper as bin/realtime-helper alone, never setuid: bin/heal
  and bin/heal.new are Schwung's heal's to make on the Move (module/standalone
  stages it).
- Nothing personal: home folders, keys, tokens.

  tools/check_package.py ASSET VERSION
"""
import json
from pathlib import PurePosixPath
import re
import sys
import tarfile

HELP_WIDTH = 20
EM_AARCH64 = 183
EXECUTABLES = ("standalone", "boot-entry.sh")
# As melodee-sample's tools/make_release.py: nothing personal or private.
FORBIDDEN = (re.compile(rb"/Use" rb"rs/[A-Za-z]|/ho" rb"me/[a-z]|/private/tmp/|/var/folders/"),
             re.compile(rb"PRIVATE KEY|PuTTY-User-Key"),
             re.compile(rb"(ssh-(ed25519|rsa|dss)|ecdsa-sha2-nistp\d+) AAAA"),
             re.compile(rb"authorized_keys|id_ed25519|id_rsa|id_ecdsa"),
             re.compile(rb"ghp_[A-Za-z0-9]{20}|github_pat_|gho_[A-Za-z0-9]{20}|AKIA[0-9A-Z]{16}|sk-ant-"))


def help_problems(text):
    try:
        data = json.loads(text)
    except ValueError as error:
        return [f"help.json: {error}"]
    if not isinstance(data, dict) or not data.get("children"):
        return ["help.json: no topics under \"children\""]
    problems = []

    def walk(node, where):
        title = node.get("title", "")
        if not isinstance(title, str) or not title:
            problems.append(f"help.json: a topic under {where} has no title")
        place = f"{where} > {title}"
        if "children" in node:
            for child in node["children"]:
                walk(child, place)
        elif isinstance(node.get("lines"), list):
            for line in node["lines"]:
                if not isinstance(line, str) or not all(" " <= c <= "~" for c in line):
                    problems.append(f"help.json: {place}: not printable ASCII: {line!r}")
                elif len(line) > HELP_WIDTH:
                    problems.append(f"help.json: {place}: over {HELP_WIDTH} characters: {line!r}")
        else:
            problems.append(f"help.json: {place} has neither children nor lines")

    for topic in data["children"]:
        walk(topic, "Help")
    return problems


def check(asset, version):
    problems = []
    with tarfile.open(asset, "r:gz") as archive:
        members = archive.getmembers()
        tops = {PurePosixPath(m.name).parts[0] for m in members}
        if len(tops) != 1:
            return [f"not one folder at the top: {sorted(tops)}"]
        top = tops.pop()
        files = {}
        for member in members:
            path = PurePosixPath(member.name)
            if path.is_absolute() or ".." in path.parts:
                problems.append(f"{member.name}: outside the folder")
            if not (member.isfile() or member.isdir()):
                problems.append(f"{member.name}: neither a file nor a folder")
            name = path.name
            if name.startswith("._") or name == ".DS_Store" or name.endswith(".log") or name.endswith(".log.old"):
                problems.append(f"{member.name}: not part of a release")
            relative = str(PurePosixPath(*path.parts[1:])) if len(path.parts) > 1 else ""
            if relative in ("config.json", "secrets") or relative.startswith("secrets/"):
                problems.append(f"{member.name}: the user's own, never shipped (Schwung's docs/MODULES.md)")
            if member.isfile():
                files[relative] = (member, archive.extractfile(member).read())

    manifest = json.loads(files["module.json"][1]) if "module.json" in files else None
    if manifest is None:
        return problems + ["no module.json"]
    if manifest.get("id") != top:
        problems.append(f"module.json's id {manifest.get('id')!r} is not the folder's name {top!r}")
    if not re.fullmatch(r"[a-z0-9-]+", top):
        problems.append(f"id {top!r}: Schwung's ids are lowercase and hyphenated")
    if manifest.get("version") != version:
        problems.append(f"module.json's version {manifest.get('version')!r}, not {version}")
    if manifest.get("component_type") != "tool" or manifest.get("standalone") is not True:
        problems.append("module.json: not a standalone tool")
    target = manifest.get("boot_target")
    if target is not None:
        name, exec_ = target.get("name", ""), target.get("exec", "")
        if not 1 <= len(name) <= 24 or not all(" " <= c <= "~" for c in name) or '"' in name or "\\" in name:
            problems.append(f"boot_target name {name!r}: 1-24 printable ASCII, no quote or backslash")
        if exec_ not in files:
            problems.append(f"boot_target exec {exec_!r}: no such file in the folder")

    for relative, (member, data) in sorted(files.items()):
        executable = member.mode & 0o111 == 0o111
        if (relative in EXECUTABLES or relative.startswith("bin/")) and not executable:
            problems.append(f"{relative}: not executable")
        if data[:4] == b"\x7fELF" and int.from_bytes(data[18:20], "little") != EM_AARCH64:
            problems.append(f"{relative}: not built for aarch64")
        for pattern in FORBIDDEN:
            match = pattern.search(data)
            if match:
                problems.append(f"{relative}: carries {match.group(0)[:40]!r}")
        if relative.startswith("lib/") and ".so" in relative and f"licenses/{relative}.txt" not in files:
            problems.append(f"{relative}: no licenses/{relative}.txt beside it")

    for relative in ("bin/heal", "bin/heal.new"):
        if relative in files:
            problems.append(f"{relative}: Schwung's heal makes it on the Move, from bin/realtime-helper")
    for relative, (member, _) in files.items():
        if member.mode & 0o7000:
            problems.append(f"{relative}: setuid, setgid or sticky")
    for required in EXECUTABLES + ("bin/melodee-move", "bin/realtime-helper", "help.json", "SOURCE.txt",
                                    "licenses/LICENSE-AGPL-3.0.txt", "licenses/THIRD_PARTY.md"):
        if required not in files:
            problems.append(f"no {required}")
    if "help.json" in files:
        problems += help_problems(files["help.json"][1].decode("utf-8", "replace"))
    return problems


def main():
    if len(sys.argv) != 3:
        print(__doc__.strip().splitlines()[-1].strip(), file=sys.stderr)
        return 2
    asset, version = sys.argv[1:]
    problems = check(asset, version)
    for problem in problems:
        print(f"check_package: {problem}", file=sys.stderr)
    if problems:
        return 1
    print(f"check_package: {asset} is a Schwung catalog asset of version {version}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
