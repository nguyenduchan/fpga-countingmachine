#!/usr/bin/env bash
set -euo pipefail

echo "Installing build packages for Kria KV260 / Ubuntu 24.04"
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config libopencv-dev v4l-utils
