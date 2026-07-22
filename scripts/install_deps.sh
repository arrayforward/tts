#!/usr/bin/env bash
set -euo pipefail

sudo apt-get update
sudo apt-get install -y \
    build-essential cmake pkg-config \
    libgrpc++-dev libprotobuf-dev protobuf-compiler protobuf-compiler-grpc \
    libssl-dev libwebsockets-dev nlohmann-json3-dev libspdlog-dev \
    libgtest-dev

echo "deps installed. continue with scripts/build.sh"