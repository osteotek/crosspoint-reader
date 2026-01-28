#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/layout_benchmark"
BINARY="$BUILD_DIR/LayoutBenchmark"

mkdir -p "$BUILD_DIR"

SOURCES=(
  "$ROOT_DIR/test/layout_benchmark/LayoutBenchmark.cpp"
  "$ROOT_DIR/lib/Epub/Epub/ParsedText.cpp"
  "$ROOT_DIR/lib/Epub/Epub/hyphenation/Hyphenator.cpp"
  "$ROOT_DIR/lib/Epub/Epub/hyphenation/LanguageRegistry.cpp"
  "$ROOT_DIR/lib/Epub/Epub/hyphenation/LiangHyphenation.cpp"
  "$ROOT_DIR/lib/Epub/Epub/hyphenation/HyphenationCommon.cpp"
  "$ROOT_DIR/lib/Utf8/Utf8.cpp"
  "$ROOT_DIR/lib/EpdFont/EpdFont.cpp"
  "$ROOT_DIR/lib/EpdFont/EpdFontFamily.cpp"
)

CXXFLAGS=(
  -std=c++20
  -O2
  -Wall
  -Wextra
  -pedantic
  -DCROSSPOINT_LAYOUT_BENCH
  -I"$ROOT_DIR/test/layout_benchmark/mocks"
  -I"$ROOT_DIR/lib"
  -I"$ROOT_DIR/lib/Utf8"
  -I"$ROOT_DIR/lib/Epub/Epub"
  -I"$ROOT_DIR/lib/EpdFont"
)

c++ "${CXXFLAGS[@]}" "${SOURCES[@]}" -o "$BINARY"

"$BINARY" "$@"
