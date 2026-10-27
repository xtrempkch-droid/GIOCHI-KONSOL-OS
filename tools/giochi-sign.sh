#!/bin/sh
# Giochi Konsol OS v1.2 — giochi-sign.sh (S12, ADR-12).
#
# Assina uma imagem/pacote com Ed25519 e gera o manifesto com SHA-256.
# DECISAO: usa `openssl pkeyutl -sign -rawin` (Ed25519 e one-shot; a chave
# fica em PEM). giochi-verify faz o mesmo caminho via libcrypto.
#
# Uso: giochi-sign.sh ARQUIVO CHAVE_PRIVADA.pem [VERSAO]
set -eu

FILE=${1:-}
KEY=${2:-}
VERSION=${3:-"1.2"}

usage() {
    echo "uso: giochi-sign.sh ARQUIVO CHAVE_PRIVADA.pem [VERSAO]" >&2
    exit 2
}

[ -n "$FILE" ] && [ -n "$KEY" ] || usage
[ -f "$FILE" ] || { echo "giochi-sign: arquivo inexistente" >&2; exit 1; }
[ -f "$KEY" ] || { echo "giochi-sign: chave privada inexistente" >&2; exit 1; }
command -v openssl >/dev/null 2>&1 || {
    echo "giochi-sign: openssl ausente" >&2; exit 1; }

openssl pkeyutl -sign -inkey "$KEY" -rawin -in "$FILE" -out "$FILE.sig"

sha=$(sha256sum "$FILE" | awk '{print $1}')
size=$(wc -c <"$FILE" | tr -d ' ')

cat > "$FILE.manifest.json" <<JSON
{
  "schema_version": "1.0",
  "package": "$(basename "$FILE")",
  "version": "$VERSION",
  "sha256": "$sha",
  "size_bytes": $size,
  "signature": "$(basename "$FILE").sig"
}
JSON

echo "assinatura: $FILE.sig"
echo "manifesto : $FILE.manifest.json"
