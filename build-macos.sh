#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
configuration="${1:-Release}"

case "$configuration" in
  Debug|Release) ;;
  *)
    echo "Usage: bash build-macos.sh [Debug|Release]" >&2
    exit 2
    ;;
esac

if ! command -v cmake >/dev/null 2>&1; then
  echo "CMake is required. Install it with: brew install cmake" >&2
  exit 1
fi

build_dir="$script_dir/out/macos-$configuration"

cmake -S "$script_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE="$configuration"
cmake --build "$build_dir" --parallel
ctest --test-dir "$build_dir" --output-on-failure

echo "Build and tests passed: $configuration"

