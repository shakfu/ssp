# sourced by the install scripts: copies to the SSP's BOOT partition, on the SD card mounted locally
# (the parent of SSP_PLUGINS, default /media/$USER/BOOT/plugins), else on SSP_HOST over ssh at /media/BOOT.
# Paths on the card are relative to BOOT. Over ssh each copy is synced and checked by md5 before it returns;
# locally, card_sync flushes it.

SSP_HOST="${SSP_HOST:-root@192.168.1.6}"
CARD_LOCAL="$(dirname "${SSP_PLUGINS:-/media/$USER/BOOT/plugins}")"
CARD_REMOTE=/media/BOOT

if [ -d "$CARD_LOCAL" ]; then
    CARD_VIA=local
    CARD_WHERE="$CARD_LOCAL"
elif ssh -o BatchMode=yes -o ConnectTimeout=5 "$SSP_HOST" "[ -d $CARD_REMOTE ]" 2>/dev/null; then
    CARD_VIA=ssh
    CARD_WHERE="$SSP_HOST:$CARD_REMOTE"
else
    echo "$CARD_LOCAL not mounted and $SSP_HOST unreachable" >&2
    exit 1
fi

# card_sh CMD: runs CMD in a shell at BOOT, on whichever side holds the card
card_sh() {
    if [ "$CARD_VIA" = local ]; then (cd "$CARD_LOCAL" && sh -c "$1"); else ssh -o BatchMode=yes "$SSP_HOST" "cd $CARD_REMOTE && $1"; fi
}

# the md5 of every file under a directory, by relative path; byte order, as busybox sorts
_card_sums() { find . -type f -exec md5sum {} + | LC_ALL=C sort -k2; }

_card_check() {  # _card_check DIR REL [NAME...]: DIR's files (or NAMEs) against BOOT/REL's
    local dir="$1" rel="$2" here there
    shift 2
    if [ $# -eq 0 ]; then
        here=$(cd "$dir" && _card_sums)
        there=$(card_sh "sync && cd '$rel' && find . -type f -exec md5sum {} + | sort -k2")
    else
        here=$(cd "$dir" && md5sum "$@")
        there=$(card_sh "sync && cd '$rel' && md5sum $(printf "'%s' " "$@")")
    fi
    [ "$here" = "$there" ] || { echo "$CARD_WHERE/$rel differs from $dir" >&2; exit 1; }
}

# card_files REL FILE...: copies files into BOOT/REL, made if missing
card_files() {
    local rel="$1"
    shift
    card_sh "mkdir -p '$rel'"
    if [ "$CARD_VIA" = local ]; then
        cp -v "$@" "$CARD_LOCAL/$rel"/
        return
    fi
    scp -O -q -o BatchMode=yes "$@" "$SSP_HOST:$CARD_REMOTE/$rel/"
    local f
    for f in "$@"; do _card_check "$(dirname "$f")" "$rel" "$(basename "$f")"; done
    echo "$# file(s) -> $CARD_WHERE/$rel"
}

# card_tree DIR REL: replaces BOOT/REL with DIR's contents
card_tree() {
    local dir="$1" rel="$2"
    if [ "$CARD_VIA" = local ]; then
        rm -rf "${CARD_LOCAL:?}/$rel"
        mkdir -p "$(dirname "$CARD_LOCAL/$rel")"
        cp -r "$dir" "$CARD_LOCAL/$rel"
        return
    fi
    tar -C "$dir" -cf - . | ssh -o BatchMode=yes "$SSP_HOST" \
        "rm -rf '$CARD_REMOTE/$rel' && mkdir -p '$CARD_REMOTE/$rel' && tar -C '$CARD_REMOTE/$rel' -xof -"
    _card_check "$dir" "$rel"
    echo "$dir -> $CARD_WHERE/$rel"
}

# card_sync: flushes a local card before it is unmounted; an ssh copy already synced
card_sync() {
    if [ "$CARD_VIA" = local ]; then sync; fi
}
