#!/usr/bin/env bash
# Build a platform-specific wheel containing the release native CLI.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTHON_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJECT_ROOT="$(cd "$PYTHON_DIR/../.." && pwd)"
PYTHON="${PYTHON:-python3}"

# shellcheck source=bindings/python/scripts/wheel_platform.sh
source "$SCRIPT_DIR/wheel_platform.sh"

default_platform_tag="$(coverwise_default_platform_tag)"
platform_tag="${COVERWISE_WHEEL_PLATFORM_TAG:-$default_platform_tag}"

# Linux tags are refined afterwards by auditwheel, but a macOS tag encodes the
# deployment target the binary was compiled with and cannot be overridden freely.
if [[ "$platform_tag" == macosx_* && "$platform_tag" != "$default_platform_tag" ]]; then
  echo "Requested macOS platform tag $platform_tag does not match the build target $default_platform_tag" >&2
  exit 1
fi

"$SCRIPT_DIR/build_native.sh"

# Apache-2.0 obliges every distributed copy to carry the license text, and
# hatchling only picks it up from the package directory.
cp "$PROJECT_ROOT/LICENSE" "$PYTHON_DIR/LICENSE"

rm -rf "$PYTHON_DIR/dist"
"$PYTHON" -m build --wheel --outdir "$PYTHON_DIR/dist" "$PYTHON_DIR"

"$PYTHON" -m wheel tags \
  --python-tag py3 \
  --abi-tag none \
  --platform-tag "$platform_tag" \
  --remove "$PYTHON_DIR"/dist/*.whl
