#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Asks a Move on Ableton's own firmware what running move-base there would
# take, without changing it: tools/probe-stock-remote.sh goes over SSH on
# stdin (nothing is copied to the Move) and only reads. The report is kept
# here in .local/probe/ (ignored by git; it describes your device).
#
#   tools/probe-stock.sh [HOST]        HOST defaults to move.local
set -eu
cd "$(dirname "$0")/.."
HOST=${1:-move.local}
mkdir -p .local/probe
REPORT=".local/probe/$HOST-$(date +%Y%m%d-%H%M%S).txt"
if ! ssh "root@$HOST" 'sh -s' < tools/probe-stock-remote.sh > "$REPORT" 2>&1; then
    echo "The probe did not finish; what it got is in $REPORT" >&2
    exit 1
fi
cat "$REPORT"
echo
echo "Report: $REPORT"
