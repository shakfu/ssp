#!/usr/bin/env bash
# copy sapf's prelude and its examples to the SSP's SD card (ssp-card.sh: mounted, or over ssh) as
# BOOT/sapf/sapf-prelude.txt and BOOT/sapf/sapf-examples.txt, where the sapf plugin looks for them.
# Synthor may run: the plugin reads them only when it loads them.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SAPF="${SAPF_DIR:-$("$ROOT/scripts/fetch_sapf.sh")}"
source "$ROOT/scripts/ssp-card.sh"

card_files sapf "$SAPF/sapf-prelude.txt" "$SAPF/sapf-examples.txt"
card_sync
