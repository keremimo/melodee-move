# SPDX-License-Identifier: AGPL-3.0-only
"""Would the Move's running kernel take the modules tools/kernel-modules.sh
built (docs/USB_AUDIO.md)? The same check insmod makes, made beforehand on a
computer: the vermagic, then the CRC of every symbol each module imports
against its exporter's, the kernel (read out of its booted Image), one of the
Move's own modules, or one of the new ones. And the build against the Move:
every symbol the kernel exports with the same CRC in both, and the gadget
modules the Move already has built again from the same source (srcversion).

    python3 -I tools/kernel_check.py CHECK_DIR KMOD_DIR

CHECK_DIR (tools/check-kernel.sh fills it): Image, kallsyms.txt (the lines
for the section symbols), release, and the Move's usb_f_ncm.ko, usb_f_fs.ko,
libcomposite.ko. KMOD_DIR: build/kmod. Exit status 0 when all of it agrees.
"""
import os
import struct
import sys

NEW = ['snd.ko', 'snd-timer.ko', 'snd-pcm.ko', 'u_audio.ko', 'usb_f_uac2.ko']
MOVE_OWN = ['libcomposite.ko', 'usb_f_ncm.ko', 'usb_f_fs.ko']


def sections(path):
    data = open(path, 'rb').read()
    shoff, = struct.unpack_from('<Q', data, 0x28)
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', data, 0x3a)
    found = []
    for n in range(shnum):
        name, kind, _, _, offset, size, link, _, _, _ = struct.unpack_from('<IIQQQQIIQQ', data, shoff + n * shentsize)
        found.append({'name': name, 'type': kind, 'off': offset, 'size': size, 'link': link})
    names = found[shstrndx]
    for section in found:
        at = names['off'] + section['name']
        section['name'] = data[at:data.index(b'\0', at)].decode()
    return data, found


def imports(path):
    """A module's __versions: each symbol it uses and the CRC it was built against."""
    data, found = sections(path)
    out = {}
    for section in found:
        if section['name'] == '__versions':
            for n in range(section['size'] // 64):
                at = section['off'] + 64 * n
                crc, = struct.unpack_from('<Q', data, at)
                out[data[at + 8:at + 64].split(b'\0')[0].decode()] = crc & 0xffffffff
    return out


def exports(path):
    """A module's own exports and their CRCs: its absolute __crc_ symbols."""
    data, found = sections(path)
    out = {}
    for section in found:
        if section['type'] != 2:  # SHT_SYMTAB
            continue
        strings = found[section['link']]
        for n in range(section['size'] // 24):
            name_at, _, _, index, value, _ = struct.unpack_from('<IBBHQQ', data, section['off'] + 24 * n)
            at = strings['off'] + name_at
            name = data[at:data.index(b'\0', at)].decode()
            if name.startswith('__crc_') and index == 0xfff1:  # SHN_ABS
                out[name[6:]] = value & 0xffffffff
    return out


def modinfo(path):
    data, found = sections(path)
    for section in found:
        if section['name'] == '.modinfo':
            text = data[section['off']:section['off'] + section['size']].decode(errors='replace')
            return dict(entry.split('=', 1) for entry in text.split('\0') if '=' in entry)
    return {}


def kernel_crcs(image_path, kallsyms_path):
    """Every symbol the kernel exports and its CRC, from its arm64 Image: the
    tables lie where kallsyms says, as offsets from _text, where the Image
    starts. Entries are three prel32 words (value, name, namespace); the CRCs
    plain words (no CONFIG_MODULE_REL_CRCS)."""
    image = open(image_path, 'rb').read()
    if image[0x38:0x3c] != b'ARMd':
        raise SystemExit(image_path + ' is not an arm64 Image')
    at = {}
    for line in open(kallsyms_path):
        fields = line.split()
        if len(fields) >= 3:
            at[fields[2]] = int(fields[0], 16)
    text = at['_text']
    size, = struct.unpack_from('<Q', image, 16)
    if '_end' in at and at['_end'] - text != size:
        raise SystemExit('this Image is not the running kernel (its size is not _end - _text)')

    def string(address):
        offset = address - text
        return image[offset:image.index(b'\0', offset)].decode()

    out = {}
    for table, crcs in (('__ksymtab', '__kcrctab'), ('__ksymtab_gpl', '__kcrctab_gpl')):
        start, stop, crc_start = at['__start_' + table], at['__stop_' + table], at['__start_' + crcs]
        for n in range((stop - start) // 12):
            entry = start + 12 * n
            _, name_offset, _ = struct.unpack_from('<iii', image, entry - text)
            out[string(entry + 4 + name_offset)] = struct.unpack_from('<I', image, crc_start - text + 4 * n)[0]
    return out


def main():
    check, kmod = sys.argv[1], sys.argv[2]
    ok = True
    release = open(os.path.join(check, 'release')).read().strip()
    kernel = kernel_crcs(os.path.join(check, 'Image'), os.path.join(check, 'kallsyms.txt'))
    print(f'The Move: {release}, {len(kernel)} symbols exported')

    owned = {}
    for name in MOVE_OWN:
        for symbol, crc in exports(os.path.join(check, name)).items():
            owned[symbol] = (crc, name)

    # The build: its kernel exports what the Move's does, every CRC the same.
    symvers = os.path.join(kmod, 'reference', 'Module.symvers')
    if os.path.exists(symvers):
        built = {}
        for line in open(symvers):
            crc, name, where = line.split('\t')[:3]
            if where == 'vmlinux':
                built[name] = int(crc, 16)
        differ = [name for name, crc in kernel.items() if built.get(name) != crc]
        extra = [name for name in built if name not in kernel]
        print(f'Built kernel: {len(kernel) - len(differ)} of {len(kernel)} CRCs the same, {len(extra)} exports the Move has not')
        if differ or extra:
            ok = False
            print('  different or missing:', ' '.join(differ[:12]), ' only built:', ' '.join(extra[:12]))
        for name in MOVE_OWN:
            mine, theirs = modinfo(os.path.join(kmod, 'reference', name)), modinfo(os.path.join(check, name))
            same = mine.get('srcversion') == theirs.get('srcversion') and mine.get('vermagic') == theirs.get('vermagic')
            print(f'{name}: built again from {"the same" if same else "OTHER"} source')
            ok = ok and same

    new_exports = {}
    for name in NEW:
        for symbol, crc in exports(os.path.join(kmod, name)).items():
            new_exports[symbol] = (crc, name)
    for name in NEW:
        path = os.path.join(kmod, name)
        info = modinfo(path)
        problems = []
        if not info.get('vermagic', '').startswith(release + ' '):
            problems.append('vermagic ' + info.get('vermagic', '?'))
        for symbol, crc in imports(path).items():
            if symbol in kernel:
                want, source = kernel[symbol], 'the kernel'
            elif symbol in owned:
                want, source = owned[symbol]
            elif symbol in new_exports:
                want, source = new_exports[symbol]
            else:
                problems.append(symbol + ': nothing exports it')
                continue
            if want != crc:
                problems.append(f'{symbol}: {crc:08x}, {source} has {want:08x}')
        print(f'{name}: {len(imports(path))} imports, ' + ('all agree' if not problems else f'{len(problems)} problems'))
        for problem in problems[:10]:
            print('  ' + problem)
        ok = ok and not problems
    print('PASS: the Move will take them' if ok else 'FAIL: do not load these on this Move')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
