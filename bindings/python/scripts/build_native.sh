#!/usr/bin/env bash
# Build the release CLI that is embedded in a platform-specific Python wheel.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTHON_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJECT_ROOT="$(cd "$PYTHON_DIR/../.." && pwd)"
BUILD_DIR="${COVERWISE_PYTHON_BUILD_DIR:-$PROJECT_ROOT/build-python}"
PACKAGE_BIN_DIR="$PYTHON_DIR/src/coverwise/_bin"

case "$(uname -s)" in
  Darwin | Linux) ;;
  *)
    echo "coverwise Python wheels are supported on Linux and macOS only" >&2
    exit 1
    ;;
esac

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF \
  -DBUILD_WASM=OFF \
  -DBUILD_CLI=ON
cmake --build "$BUILD_DIR" --parallel

mkdir -p "$PACKAGE_BIN_DIR"
install -m 755 "$BUILD_DIR/bin/coverwise" "$PACKAGE_BIN_DIR/coverwise"
