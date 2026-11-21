#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Builds a release of melodee-move for Schwung's module catalog from a
# commit, never from the working tree, which other work may be changing:
#
#   tools/release.sh [REF]      REF: a commit or tag, by default HEAD
#
# The version is module/module.json's at REF; release.json (what Schwung
# Manager reads from main) must name it and its asset, and CHANGELOG.md must
# have its section. REF is exported (git archive) into
# build/release/vX.Y.Z/src with the pinned dependencies copied beside it and
# ellic-core at the commit REF's core/ submodule names, which must be on
# core's origin: it is the release's source too (SOURCE.txt in the asset).
# The full check (./build.sh) runs there. melodee-sample, exported at a
# commit on its origin (MELODEE_SAMPLE_REF, by default origin/main), only
# draws the web page's wordmark; without the checkout the page names
# Melodee in text. Left in
# build/release/vX.Y.Z/:
#
#   melodee-move-module.tar.gz          the asset the catalog downloads
#   melodee-move-module.tar.gz.sha256
#   release.json                        as committed at REF
#   notes.md                            the GitHub release's text
#
# It tags, pushes and publishes nothing: it prints what would.
set -eu
cd "$(dirname "$0")/.."
die() {
    echo "release: $*" >&2
    exit 1
}
REF=${1:-HEAD}
COMMIT=$(git rev-parse --verify --quiet "$REF^{commit}") || die "no commit $REF"
# A string field of a JSON file at the commit, as the Makefile reads one.
field() { git show "$COMMIT:$1" | sed -n "s/.*\"$2\": *\"\([^\"]*\)\".*/\1/p" | head -n 1; }
ID=$(field module/module.json id)
VERSION=$(field module/module.json version)
TAG=v$VERSION
ASSET=$ID-module.tar.gz
URL=$(field release.json download_url)
[ "$(field release.json version)" = "$VERSION" ] || die "release.json is version $(field release.json version), module.json $VERSION"
case $URL in
https://github.com/*/*/releases/download/$TAG/$ASSET) ;;
*) die "release.json's download_url is not $TAG's $ASSET: $URL" ;;
esac
REPO=$(echo "$URL" | cut -d/ -f4-5)
git show "$COMMIT:CHANGELOG.md" | grep -q "^## $VERSION " || die "CHANGELOG.md has no \"## $VERSION\" section"

published() { # published REPOSITORY COMMIT
    git -C "$1" fetch --quiet origin || die "cannot reach $1's origin to see what is there"
    [ -n "$(git -C "$1" branch -r --contains "$2" 2> /dev/null)" ] || die "$1's $2 is not on its origin"
}
# ellic-core: the commit REF pins, on its origin.
ELLIC_CORE_COMMIT=$(git rev-parse --verify --quiet "$COMMIT:core") || die "$REF has no core/ submodule"
[ -f core/core.mk ] || git submodule update --init core
published core "$ELLIC_CORE_COMMIT"
# melodee-sample, for the wordmark: a commit on its origin, if there is a checkout.
CHECKOUT=""
if [ -d "${MELODEE_SAMPLE:-../melodee-sample}/.git" ]; then
    CHECKOUT=$(cd "${MELODEE_SAMPLE:-../melodee-sample}" && pwd)
    git -C "$CHECKOUT" fetch --quiet origin || die "cannot reach $CHECKOUT's origin"
    MELODEE_SAMPLE_COMMIT=$(git -C "$CHECKOUT" rev-parse --verify --quiet "${MELODEE_SAMPLE_REF:-origin/main}^{commit}") ||
        die "melodee-sample has no ${MELODEE_SAMPLE_REF:-origin/main}"
fi
EPOCH=$(git log -1 --format=%ct "$COMMIT")

OUT=build/release/$TAG
echo "release: $ID $VERSION from $(git rev-parse --short "$COMMIT"), ellic-core $(echo "$ELLIC_CORE_COMMIT" | cut -c1-7), in $OUT"
rm -rf "$OUT"
mkdir -p "$OUT/src/build"
git archive "$COMMIT" | tar -x -C "$OUT/src"
# The pinned downloads (tools/dependencies.py checks them), cloned where the
# file system can.
if [ -d build/deps ]; then
    cp -Rc build/deps "$OUT/src/build/" 2> /dev/null || cp -R build/deps "$OUT/src/build/"
fi
mkdir -p "$OUT/src/core"
git -C core archive "$ELLIC_CORE_COMMIT" | tar -x -C "$OUT/src/core"
MELODEE_SAMPLE=$(pwd)/$OUT/melodee-sample
mkdir -p "$MELODEE_SAMPLE"
[ -z "$CHECKOUT" ] || git -C "$CHECKOUT" archive "$MELODEE_SAMPLE_COMMIT" tools docs/images/melodee-wordmark.png | tar -x -C "$MELODEE_SAMPLE"
(cd "$OUT/src" && MELODEE_SAMPLE="$MELODEE_SAMPLE" MELODEE_MOVE_COMMIT="$COMMIT" ELLIC_CORE_COMMIT="$ELLIC_CORE_COMMIT" \
    MELODEE_MOVE_EPOCH="$EPOCH" ./build.sh)

cp "$OUT/src/dist/$ASSET" "$OUT/"
SHA256=$(shasum -a 256 "$OUT/$ASSET" | cut -d' ' -f1)
echo "$SHA256  $ASSET" > "$OUT/$ASSET.sha256"
git show "$COMMIT:release.json" > "$OUT/release.json"
{
    # The version's section, without its heading or the blank lines around it.
    git show "$COMMIT:CHANGELOG.md" | awk -v v="## $VERSION " '
        index($0, v) == 1 { on = 1; next }
        /^## / { on = 0 }
        on && $0 == "" { blank++; next }
        on { if (text && blank) printf "\n"; blank = 0; text = 1; print }'
    cat << EOF

### Install

In Schwung Manager (http://move.local:7700), **Modules**: Melodee, under
Tools. Without the catalog: **Install Custom Module**, From GitHub URL,
\`https://github.com/$REPO\`. Then Tools > Melodee on the Move.

### Source

Free software under the GNU AGPL 3.0. Built from $REPO $(git rev-parse --short "$COMMIT"),
with Melodee's engine and model from keremimo/ellic-core $(echo "$ELLIC_CORE_COMMIT" | cut -c1-7)
(\`SOURCE.txt\` in the asset).

SHA-256 of \`$ASSET\`: \`$SHA256\`
EOF
} > "$OUT/notes.md"

echo "release: $OUT/$ASSET ($(du -k "$OUT/$ASSET" | cut -f1) KB), sha256 $SHA256"
echo "release: to publish, once approved (nothing here was run):"
echo "    git tag -s $TAG $COMMIT -m \"melodee-move $VERSION\""
echo "    git push origin main $TAG"
echo "    gh release create $TAG $OUT/$ASSET --repo $REPO --title \"Melodee $VERSION\" --notes-file $OUT/notes.md"
