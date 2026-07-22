#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/build"

ctest --test-dir "${BUILD}" --output-on-failure -j"$(nproc)"