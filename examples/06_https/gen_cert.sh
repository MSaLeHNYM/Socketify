#!/usr/bin/env bash
# Generate a self-signed certificate for local HTTPS development.
set -euo pipefail

OUT_DIR="${1:-.}"
openssl req -x509 -newkey rsa:2048 -nodes -batch \
    -keyout "${OUT_DIR}/server.key" \
    -out "${OUT_DIR}/server.crt" \
    -days 365 \
    -subj "/CN=localhost"

echo "wrote ${OUT_DIR}/server.crt and ${OUT_DIR}/server.key"
