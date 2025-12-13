#!/bin/bash
# Script to compile and run native hyphenation tests manually

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$SCRIPT_DIR/../.."

echo "Compiling hyphenation tests..."

# Clone Unity test framework if not exists
if [ ! -d "/tmp/Unity" ]; then
  echo "Downloading Unity test framework..."
  git clone --depth 1 https://github.com/ThrowTheSwitch/Unity.git /tmp/Unity
fi

# Compile
g++ -std=c++2a -DUNIT_TEST \
  -Itest/test_native_hyphenation \
  -Ilib -Ilib/EpdFont -Ilib/Utf8 \
  -Isrc -I. -I/tmp/Unity/src \
  test/test_native_hyphenation/test_main.cpp \
  /tmp/Unity/src/unity.c \
  -o /tmp/test_hyphenation

echo "Running tests..."
/tmp/test_hyphenation

echo "All tests passed!"
