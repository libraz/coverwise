#!/usr/bin/env bash
# Build a platform-specific wheel containing the release native CLI.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTHON_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PYTHON="${PYTHON:-python3}"

"$SCRIPT_DIR/build_native.sh"

rm -rf "$PYTHON_DIR/dist"
"$PYTHON" -m build --wheel --outdir "$PYTHON_DIR/dist" "$PYTHON_DIR"

if [[ -n "${COVERWISE_WHEEL_PLATFORM_TAG:-}" ]]; then
  platform_tag="$COVERWISE_WHEEL_PLATFORM_TAG"
elif [[ "$(uname -s)" == "Darwin" ]]; then
  case "$(uname -m)" in
    arm64) platform_tag="macosx_11_0_arm64" ;;
    x86_64) platform_tag="macosx_10_15_x86_64" ;;
    *) echo "Unsupported macOS architecture: $(uname -m)" >&2; exit 1 ;;
  esac
else
  platform_tag="linux_$(uname -m)"
fi

"$PYTHON" -m wheel tags \
  --python-tag py3 \
  --abi-tag none \
  --platform-tag "$platform_tag" \
  --remove "$PYTHON_DIR"/dist/*.whl
