#!/usr/bin/env bash
# Build the release CLI that is embedded in a platform-specific Python wheel.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTHON_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJECT_ROOT="$(cd "$PYTHON_DIR/../.." && pwd)"
BUILD_DIR="${COVERWISE_PYTHON_BUILD_DIR:-$PROJECT_ROOT/build-python}"
PACKAGE_BIN_DIR="$PYTHON_DIR/src/coverwise/_bin"

# shellcheck source=bindings/python/scripts/wheel_platform.sh
source "$SCRIPT_DIR/wheel_platform.sh"

configure_args=(
  -DCMAKE_BUILD_TYPE=Release
  -DBUILD_TESTING=OFF
  -DBUILD_WASM=OFF
  -DBUILD_CLI=ON
)

case "$(uname -s)" in
  Darwin)
    # Without an explicit target the binary records the build host's OS version
    # as its minimum, which contradicts the wheel tag pip matches against.
    configure_args+=("-DCMAKE_OSX_DEPLOYMENT_TARGET=$COVERWISE_MACOS_DEPLOYMENT_TARGET")
    ;;
  Linux) ;;
  *)
    echo "coverwise Python wheels are supported on Linux and macOS only" >&2
    exit 1
    ;;
esac

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" "${configure_args[@]}"
cmake --build "$BUILD_DIR" --parallel

mkdir -p "$PACKAGE_BIN_DIR"
install -m 755 "$BUILD_DIR/bin/coverwise" "$PACKAGE_BIN_DIR/coverwise"
