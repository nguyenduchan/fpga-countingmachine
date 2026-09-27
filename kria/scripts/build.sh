#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
cmake -S "$root/kria" -B "$root/kria/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$root/kria/build" -j"$(nproc)"
echo "Binary: $root/kria/build/kria_eth_camera"
