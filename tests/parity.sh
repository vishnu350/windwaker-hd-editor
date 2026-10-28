#!/bin/sh
# Parity + behaviour checks. Byte-for-byte comparison needs node and vendor/save-format.js.
set -eu

cd "$(dirname "$0")/.."
BIN=./wwhd-edit
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

if [ "$save_digest_before" != "$(cksum "$SAVE")" ]; then
    echo "FAIL     sample save under test was modified"
    fail=1
fi

[ "$fail" -eq 0 ] && echo "all checks passed"
exit "$fail"
