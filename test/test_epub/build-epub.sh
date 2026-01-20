#!/usr/bin/env bash
set -euo pipefail

EPUB_NAME="crosspoint-test.epub"
ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"

cd "$ROOT_DIR"

if [[ ! -f mimetype ]]; then
  echo "❌ mimetype file not found"
  exit 1
fi

if [[ ! -d META-INF || ! -d OEBPS ]]; then
  echo "❌ META-INF or OEBPS directory missing"
  exit 1
fi

rm -f "$EPUB_NAME"

echo "📦 Building EPUB: $EPUB_NAME"

zip -X0 "$EPUB_NAME" mimetype

zip -Xr9D "$EPUB_NAME" META-INF OEBPS

echo "✅ EPUB built successfully"
echo "📘 Output: $EPUB_NAME"