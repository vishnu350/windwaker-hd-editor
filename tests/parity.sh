#!/bin/sh
# Parity + behaviour checks. Byte-for-byte comparison needs node and tests/save-format.js.
set -eu

cd "$(dirname "$0")/.."
BIN=./wwhd-editor-cli
SAVE=${SAVE:-cking.sav}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail=0
save_digest_before=$(cksum "$SAVE")

# check NAME '<json edits>' <cli args...>  - C output must match the Node reference byte for byte
check() {
    name=$1
    json=$2
    shift 2
    cp "${IN:-$SAVE}" "$TMP/in.sav"
    node tests/node_apply.mjs "${IN:-$SAVE}" "$TMP/node.sav" "$json"
    "$BIN" "$@" "$TMP/in.sav" --out "$TMP/c.sav" --yes >/dev/null
    if cmp -s "$TMP/node.sav" "$TMP/c.sav"; then
        echo "ok       $name"
    else
        echo "FAIL     $name"
        cmp "$TMP/node.sav" "$TMP/c.sav" || true
        fail=1
    fi
}

# expect NAME <expected-status> <input-file> <cli args...>  - the tool must fail or succeed as stated
expect() {
    name=$1
    want=$2
    infile=$3
    shift 3
    if "$BIN" "$@" "$infile" --out "$TMP/out.sav" --yes >/dev/null 2>"$TMP/err"; then
        got=0
    else
        got=1
    fi
    if [ "$got" = "$want" ]; then
        echo "ok       $name"
    else
        echo "FAIL     $name (expected exit $want, got $got: $(cat "$TMP/err"))"
        fail=1
    fi
}

if command -v node >/dev/null 2>&1; then
    check "stats, slot 1" \
        '{"slot":0,"edits":{"displayName":"Hero","maxHearts":11.75,"currentHearts":8.5,"rupees":1172,"walletLevel":2,"maxMagic":32,"currentMagic":1,"arrows":97,"maxArrows":99,"bombs":99,"maxBombs":99}}' \
        --name Hero --hearts 11.75 --health 8.5 --rupees 1172 --wallet 5000 \
        --magic-max 32 --magic 1 --arrows 97 --arrows-max 99 --bombs 99 --bombs-max 99

    check "inventory and equipment" \
        '{"slot":0,"edits":{"inventory":{"sail":119,"pictobox":35,"bottle1":85,"bottle2":87,"hookshot":47,"skullHammer":51},"equipment":{"sword":62,"shield":60,"bracelets":40}}}' \
        --item sail=0x77 --item pictobox=0x23 --item bottle1=0x55 --item bottle2=0x57 \
        --item hookshot=0x2F --item skullHammer=0x33 --equip sword=0x3E --equip shield=0x3C \
        --equip bracelets=0x28

    check "slot 2 name only" \
        '{"slot":1,"edits":{"displayName":"Tetra"}}' \
        --slot 2 --name Tetra

    check "dependent values normalize" \
        '{"slot":0,"edits":{"maxHearts":3,"currentHearts":20,"walletLevel":0,"rupees":5000,"maxMagic":16,"currentMagic":32,"maxArrows":30,"arrows":99,"maxBombs":30,"bombs":99}}' \
        --hearts 3 --health 20 --wallet 200 --rupees 5000 --magic-max 16 --magic 32 \
        --arrows-max 30 --arrows 99 --bombs-max 30 --bombs 99

    check "repair all checksums" \
        '{"repair":true}' \
        --repair

    if command -v python3 >/dev/null 2>&1; then
        # flip a byte inside quest log 1 so its checksum stops matching
        cp "$SAVE" "$TMP/corrupt.sav"
        python3 -c 'import sys;p=sys.argv[1];b=bytearray(open(p,"rb").read());b[0x100]^=0xFF;open(p,"wb").write(b)' "$TMP/corrupt.sav"
        IN=$TMP/corrupt.sav check "repairs a broken checksum" \
            '{"repair":true}' \
            --repair

        # an inventory byte the editor does not know about must survive an unrelated edit
        cp "$SAVE" "$TMP/unknown.sav"
        python3 -c 'import sys;p=sys.argv[1];b=bytearray(open(p,"rb").read());b[0x3C]=0xAA;open(p,"wb").write(b)' "$TMP/unknown.sav"
        IN=$TMP/unknown.sav check "keeps an unknown inventory byte" \
            '{"slot":0,"edits":{"rupees":25,"inventory":{"telescope":170}}}' \
            --rupees 25 --item telescope=0xAA
        if "$BIN" --info "$TMP/unknown.sav" | grep -q "Unknown 0xAA"; then
            echo "ok       reports unknown inventory values"
        else
            echo "FAIL     unknown inventory value not reported"
            fail=1
        fi
    fi
else
    echo "skip     node parity (node not found)"
fi

head -c 100 "$SAVE" >"$TMP/short.sav"
expect "rejects wrong file size" 1 "$TMP/short.sav" --rupees 10
expect "rejects a missing input file" 1 "$TMP/nope.sav" --rupees 10
expect "rejects a 9-character name" 1 "$SAVE" --name Toolthetz
expect "rejects an empty name" 1 "$SAVE" --name ""
expect "rejects non-quarter hearts" 1 "$SAVE" --hearts 3.3
expect "rejects unsupported item value" 1 "$SAVE" --item sail=0x7A
expect "rejects unknown item key" 1 "$SAVE" --item sailboat=0x77
expect "rejects magic meter of 20" 1 "$SAVE" --magic-max 20
expect "accepts a valid edit" 0 "$SAVE" --rupees 999

cp "$SAVE" "$TMP/in.sav"
"$BIN" --rupees 42 "$TMP/in.sav" --out "$TMP/edited.sav" >/dev/null
if [ "$(wc -c <"$TMP/edited.sav")" -eq 8966 ]; then
    echo "ok       output keeps the 8966-byte layout"
else
    echo "FAIL     output size changed"
    fail=1
fi
if [ "$(cmp -s "$SAVE" "$TMP/in.sav" && echo same)" = "same" ]; then
    echo "ok       input file is never modified"
else
    echo "FAIL     input file was modified"
    fail=1
fi
if "$BIN" --info "$TMP/edited.sav" | grep -q "checksum needs repair"; then
    echo "FAIL     checksum invalid after edit"
    fail=1
else
    echo "ok       checksums valid after edit"
fi

# the input file is protected, and an existing output needs an explicit --yes
if "$BIN" --rupees 42 "$TMP/in.sav" --out "$TMP/in.sav" >/dev/null 2>&1; then
    echo "FAIL     overwrote the input file"
    fail=1
else
    echo "ok       refuses to overwrite the input file"
fi
if "$BIN" --rupees 42 "$TMP/in.sav" --out "$TMP/edited.sav" >/dev/null 2>&1; then
    echo "FAIL     overwrote an existing output file"
    fail=1
else
    echo "ok       refuses to overwrite an existing output file"
fi

# The game re-derives variant items from the obtained mask at 0x51 + slot, so writes must keep it.
byte_at() { od -An -tu1 -j"$2" -N1 "$1" | tr -d ' \n'; }

expect_byte() {
    name=$1
    file=$2
    offset=$3
    want=$4
    got=$(byte_at "$file" "$offset")
    if [ "$got" = "$want" ]; then
        echo "ok       $name"
    else
        echo "FAIL     $name (offset $offset: expected $want, got $got)"
        fail=1
    fi
}

cp "$SAVE" "$TMP/v1.sav"
"$BIN" --item sail=0x78 "$TMP/v1.sav" --out "$TMP/v1_out.sav" --yes >/dev/null
expect_byte "sail downgrade writes 0x78" "$TMP/v1_out.sav" 61 120
expect_byte "sail downgrade clears the swift bit" "$TMP/v1_out.sav" 82 1

"$BIN" --item sail=0xFF "$TMP/v1.sav" --out "$TMP/v1_none.sav" --yes >/dev/null
expect_byte "removing the sail clears the item byte" "$TMP/v1_none.sav" 61 255
expect_byte "removing the sail clears the owned mask" "$TMP/v1_none.sav" 82 0

"$BIN" --item bow=0x36 "$TMP/v1.sav" --out "$TMP/v1_bow.sav" --yes >/dev/null
expect_byte "light arrows set the bow item" "$TMP/v1_bow.sav" 72 54
expect_byte "light arrows set the bow owned mask" "$TMP/v1_bow.sav" 93 7

"$BIN" --item pictobox=0x26 "$TMP/v1.sav" --out "$TMP/v1_picto.sav" --yes >/dev/null
expect_byte "deluxe picto box owned mask" "$TMP/v1_picto.sav" 89 3

# single-item slots: bit 0 of the mask keeps the item alive, the other bits must survive
"$BIN" --item baitbag=0xFF "$TMP/v1.sav" --out "$TMP/v1_drop.sav" --yes >/dev/null
expect_byte "removing the bait bag clears the item byte" "$TMP/v1_drop.sav" 71 255
expect_byte "removing the bait bag clears its mask bit" "$TMP/v1_drop.sav" 92 0
"$BIN" --item hookshot=0x2F "$TMP/v1.sav" --out "$TMP/v1_hook.sav" --yes >/dev/null
expect_byte "hookshot grant writes the item byte" "$TMP/v1_hook.sav" 79 47
expect_byte "hookshot grant sets mask bit 0" "$TMP/v1_hook.sav" 100 1
"$BIN" --item telescope=0xFF "$TMP/v1.sav" --out "$TMP/v1_tele.sav" --yes >/dev/null
expect_byte "telescope removal keeps the unrelated mask bits" "$TMP/v1_tele.sav" 81 164
"$BIN" --item bottle3=0x50 "$TMP/v1.sav" --out "$TMP/v1_bot.sav" --yes >/dev/null
expect_byte "bottle contents are written" "$TMP/v1_bot.sav" 76 80
expect_byte "bottle mask stays untouched" "$TMP/v1_bot.sav" 96 0

# a save written by the old editor (item byte downgraded, mask untouched) must be flagged
if command -v python3 >/dev/null 2>&1; then
    cp "$SAVE" "$TMP/stale.sav"
    python3 -c 'import sys;p=sys.argv[1];b=bytearray(open(p,"rb").read());b[0x3d]=0x78;open(p,"wb").write(b)' "$TMP/stale.sav"
    "$BIN" --repair "$TMP/stale.sav" --out "$TMP/stale_fixed.sav" --yes >/dev/null
    if "$BIN" --info "$TMP/stale_fixed.sav" | grep -q 'mask still claims Swift Sail'; then
        echo "ok       warns when the mask contradicts the item byte"
    else
        echo "FAIL     no warning for a contradictory mask"
        fail=1
    fi
    if "$BIN" --info "$TMP/v1_out.sav" | grep -q 'mask still claims'; then
        echo "FAIL     warning shown for a consistent save"
        fail=1
    else
        echo "ok       no warning after a correct downgrade"
    fi
fi

# GUI checks. xvfb-run's default 8-bit screen leaves ImGui with a 0x0 display, so ask for 24-bit.
gui_run() {
    if [ -n "${DISPLAY:-}" ]; then
        "$@"
    else
        xvfb-run -a -s "-screen 0 1280x800x24" "$@"
    fi
}

if [ -x ./wwhd-editor ] && { [ -n "${DISPLAY:-}" ] || command -v xvfb-run >/dev/null 2>&1; };
then
    cp "$SAVE" "$TMP/gui_in.sav"
    WWHD_GUI_FRAMES=3 gui_run ./wwhd-editor "$TMP/gui_in.sav" >"$TMP/gui.log" 2>&1 || true
    if grep -qE 'Rendered 3 frame\(s\) at [1-9][0-9]*x[1-9][0-9]*' "$TMP/gui.log"; then
        echo "ok       gui renders three frames"
    else
        echo "FAIL     gui did not render: $(tail -3 "$TMP/gui.log" | tr '\n' ' ')"
        fail=1
    fi

    # the widgets mirror the loaded file, so an untouched GUI must save it back byte-identical
    cp "$SAVE" "$TMP/gui_round_in.sav"
    WWHD_GUI_FRAMES=3 WWHD_GUI_AUTOSAVE="$TMP/gui_out.sav" gui_run ./wwhd-editor \
        "$TMP/gui_round_in.sav" >"$TMP/gui_save.log" 2>&1 || true
    if [ -f "$TMP/gui_out.sav" ] && cmp -s "$TMP/gui_out.sav" "$SAVE"; then
        echo "ok       gui round-trips the sample save unchanged"
    else
        echo "FAIL     gui output differs from the save it loaded: \
$(cmp -l "$TMP/gui_out.sav" "$SAVE" 2>&1 | head -2 | tr '\n' ' ')"
        fail=1
    fi
else
    echo "skip     gui tests (no display, no xvfb-run)"
fi

if [ "$save_digest_before" != "$(cksum "$SAVE")" ]; then
    echo "FAIL     sample save under test was modified"
    fail=1
fi

[ "$fail" -eq 0 ] && echo "all checks passed"
exit "$fail"
