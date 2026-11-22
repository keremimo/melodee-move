#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Packs build/melodee-move for the Move, in the build image (./build.sh):
#   dist/melodee-move/              run over SSH with tools/deploy-stock.sh
#                                   (or on Armbian: ./melodee-move --backend jack)
#   dist/schwung-tool/melodee-move/ a Schwung standalone tool
#                                   (tools/install-schwung-tool.sh)
#   dist/melodee-move-module.tar.gz the same tool as Schwung's module catalog
#                                   downloads it (tools/release.sh makes a
#                                   release of it; tools/check_package.py)
# The shared libraries Ableton's firmware lacks go in lib/, each with its
# licence in licenses/lib/; glibc and the loader are the Move's own.
set -eu
cd "$(dirname "$0")/.."
OUT=dist/melodee-move
TOOL=dist/schwung-tool/melodee-move
rm -rf dist
mkdir -p "$OUT/bin" "$OUT/lib" "$OUT/licenses/lib" "$TOOL"
cp build/melodee-move "$OUT/bin/"

# The Ubuntu package a library came from, whose copyright file is its
# licence. Ubuntu 22.04 merged /lib into /usr/lib, and dpkg knows each file
# by one of the two.
package_of() {
    real=$(readlink -f "$1")
    for path in "$1" "$real" "/usr$real" "${real#/usr}"; do
        package=$(dpkg -S "$path" 2> /dev/null | head -n 1 | cut -d: -f1)
        [ -n "$package" ] && { echo "$package"; return 0; }
    done
    echo "bundle: no package owns $1, so there is no licence to ship with it" >&2
    return 1
}
ldd build/melodee-move | awk '$3 ~ /^\// { print $3 }' \
    | grep -vE '/(libc|libm|libdl|libpthread|librt)\.so|/ld-linux' \
    | while read -r library; do
        cp -L "$library" "$OUT/lib/"
        package=$(package_of "$library")
        cp "/usr/share/doc/$package/copyright" "$OUT/licenses/lib/$(basename "$library").txt"
    done
# fontconfig wants a configuration; melodee-move draws with its own pixel font.
printf '<?xml version="1.0"?>\n<fontconfig></fontconfig>\n' > "$OUT/lib/fonts.conf"
# Melodee's licence and the third parts' (THIRD_PARTY.md): JUCE and QuickJS
# are linked in, Rubik is carried by the web page (tools/web_bundle.py),
# Schwung's code is adapted in src/app/schwung_pages/host.js and
# src/app/schwung_display.cpp.
cp LICENSE "$OUT/licenses/LICENSE-AGPL-3.0.txt"
cp THIRD_PARTY.md "$OUT/licenses/THIRD_PARTY.md"
cp core/LICENSE "$OUT/licenses/ellic-core-LICENSE.txt"
cp build/deps/JUCE/LICENSE.md "$OUT/licenses/JUCE-LICENSE.md"
cp build/deps/quickjs/LICENSE "$OUT/licenses/QuickJS-LICENSE.txt"
cp build/deps/rubik/OFL.txt "$OUT/licenses/Rubik-OFL.txt"
cp build/deps/schwung/LICENSE "$OUT/licenses/Schwung-LICENSE.txt"

# Where this build's source is, as the AGPL asks of a program given out:
# the commits ./build.sh passes in (tools/release.sh: a release's, clean),
# on the repositories release.json's download_url names.
ID=$(sed -n 's/.*"id": *"\([^"]*\)".*/\1/p' module/module.json)
VERSION=$(sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' module/module.json)
OWNER=$(sed -n 's|.*"download_url": *"https://github.com/\([^/]*\)/.*|\1|p' release.json)
REPOSITORY=$(sed -n 's|.*"download_url": *"https://github.com/[^/]*/\([^/]*\)/.*|\1|p' release.json)
cat > "$OUT/SOURCE.txt" << EOF
Melodee for Ableton Move $VERSION, by Kerem Kiliç (Ellic Studio).

Free software under the GNU Affero General Public License 3.0
(licenses/LICENSE-AGPL-3.0.txt). This build's source:

  https://github.com/$OWNER/$REPOSITORY/tree/${MELODEE_MOVE_COMMIT:-unknown}
  Melodee's engine and model (ellic-core), compiled unchanged:
  https://github.com/$OWNER/ellic-core/tree/${ELLIC_CORE_COMMIT:-unknown}

The third parts and their licences: licenses/THIRD_PARTY.md, licenses/.
EOF

cp tools/launcher.sh "$OUT/melodee-move"
cp tools/run-stock.sh "$OUT/run-stock.sh"
cp -R "$OUT/bin" "$OUT/lib" "$OUT/licenses" "$OUT/SOURCE.txt" "$TOOL/"
cp module/module.json module/help.json module/standalone module/boot-entry.sh "$TOOL/"
# Power-on's entry (module/boot-entry.sh) starts again through it.
cp build/unblock-signals "$TOOL/bin/"
# The privileged helper (src/boot/heal.c), which module/standalone stages
# for Schwung's heal to install as bin/heal.
cp build/realtime-helper "$TOOL/bin/"
chmod 755 "$OUT/melodee-move" "$OUT/run-stock.sh" "$TOOL/standalone" "$TOOL/boot-entry.sh" \
    "$OUT/bin/melodee-move" "$TOOL/bin/melodee-move" "$TOOL/bin/unblock-signals" "$TOOL/bin/realtime-helper"

# The catalog's asset (Schwung's docs/MODULES.md, "Publishing to the Module
# Catalog"): <id>-module.tar.gz, the tool's folder under the module's id,
# its programs stripped (the dynamic symbols stay: Schwung's modules look up
# schwung_move_info there). Owner, order and times fixed, so a commit packs
# the same bytes again; ustar, which the Move's BusyBox tar reads.
ASSET=dist/$ID-module.tar.gz
STAGE=build/package
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp -R "$TOOL" "$STAGE/$ID"
strip "$STAGE/$ID/bin/melodee-move" "$STAGE/$ID/bin/unblock-signals" "$STAGE/$ID/bin/realtime-helper"
nm -D --defined-only "$STAGE/$ID/bin/melodee-move" | grep -q ' schwung_move_info$' \
    || { echo "bundle: the stripped melodee-move lost schwung_move_info" >&2; exit 1; }
tar --sort=name --owner=0 --group=0 --numeric-owner --mode='u+rwX,go+rX,go-w' \
    --mtime="@${MELODEE_MOVE_EPOCH:-$(date +%s)}" --format=ustar -C "$STAGE" -cf - "$ID" | gzip -9n > "$ASSET"
python3 tools/check_package.py "$ASSET" "$VERSION"
echo "bundle: $OUT ($(ls "$OUT/lib" | wc -l) libraries), $TOOL, $ASSET ($(du -k "$ASSET" | cut -f1) KB)"
