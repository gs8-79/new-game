#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
configuration="${1:-Release}"

bash "$script_dir/build-formal-macos.sh" "$configuration"
cd "$script_dir"
exec "$script_dir/out/macos-formal-$configuration/tribe-dawn"
