#!/usr/bin/env bash
# Shared platform mapping sourced by build_native.sh and build_wheel.sh.
#
# A wheel platform tag is a promise about the oldest system the payload runs on.
# Nothing enforces that promise for a bundled executable, so the compiler and the
# tag must be driven from a single mapping or macOS wheels advertise a minimum
# they cannot honor.

# The floor is macOS 14: std::to_chars for floating point is unavailable before
# macOS 13.3, and pip only ever matches macOS tags whose minor version is zero.
COVERWISE_MACOS_DEPLOYMENT_TARGET=14.0

coverwise_default_platform_tag() {
  if [[ "$(uname -s)" == "Darwin" ]]; then
    echo "macosx_${COVERWISE_MACOS_DEPLOYMENT_TARGET//./_}_$(uname -m)"
  else
    echo "linux_$(uname -m)"
  fi
}
