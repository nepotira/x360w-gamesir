#!/bin/bash
set -e
cd "$(dirname "$0")/.."

/usr/local/bin/zig cc -target x86_64-macos -O2 -Wall -fblocks -o x360w \
    src/x360w.c src/vhid.c \
    -Isrc -I/usr/local/include \
    -L/usr/local/lib -lusb-1.0 -lpthread

echo "[build] x360w compilado com sucesso!"

cp x360w x360w-signed
codesign -s - --entitlements entitlements.plist -f x360w-signed
echo "[build] x360w-signed assinado com sucesso (ad-hoc + com.apple.developer.hid.virtual.device)"
