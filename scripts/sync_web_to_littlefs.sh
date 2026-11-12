#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WEB_DIR="$ROOT_DIR/web"
DATA_DIR="$ROOT_DIR/firmware/data"

echo "[1/3] Building React web app..."
(cd "$WEB_DIR" && npm run build)

echo "[2/3] Syncing gzipped static assets to firmware/data/..."
rm -rf "$DATA_DIR"
mkdir -p "$DATA_DIR/assets"

gzip -9 -c "$WEB_DIR/dist/index.html" > "$DATA_DIR/index.html.gz"
for f in "$WEB_DIR/dist/assets/"*; do
  base="$(basename "$f")"
  gzip -9 -c "$f" > "$DATA_DIR/assets/$base.gz"
done

echo "[3/3] LittleFS payload sizes:"
ls -lh "$DATA_DIR" "$DATA_DIR/assets"

