#!/bin/sh
# Giochi Konsol OS v1.2 — giochi-keys.sh (S12): gera par Ed25519 de update.
# A chave publica crua (32 bytes) e instalada em /etc/giochi/pubkey.ed25519.
set -eu

OUT=${1:-keys}
mkdir -p "$OUT"

openssl genpkey -algorithm ed25519 -out "$OUT/giochi_ed25519.pem"
openssl pkey -in "$OUT/giochi_ed25519.pem" -pubout -outform DER \
    | tail -c 32 > "$OUT/pubkey.ed25519"

chmod 600 "$OUT/giochi_ed25519.pem"
echo "chave privada: $OUT/giochi_ed25519.pem (NUNCA versionar)"
echo "chave publica: $OUT/pubkey.ed25519 -> /etc/giochi/pubkey.ed25519"
