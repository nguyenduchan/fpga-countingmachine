#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
binary="$root/kria/build/kria_eth_camera"
if [[ ! -x "$binary" ]]; then
    echo "Build first: bash kria/scripts/build.sh"
    exit 1
fi

sudo install -d /etc/kria-ethernet-camera
sudo install -m 644 "$root/config/board.conf" /etc/kria-ethernet-camera/board.conf
sudo install -m 755 "$binary" /usr/local/bin/kria_eth_camera
sudo install -m 644 "$root/kria/deploy/kria-ethernet-camera.service" /etc/systemd/system/kria-ethernet-camera.service
sudo systemctl daemon-reload
sudo systemctl enable --now kria-ethernet-camera.service
sudo systemctl --no-pager --full status kria-ethernet-camera.service
