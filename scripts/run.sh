#!/usr/bin/env bash
set -euo pipefail

if [[ -z "${MINIMAX_API_KEY:-}" ]]; then
    echo "MINIMAX_API_KEY env is required"; exit 1
fi
if [[ -z "${GRPC_JWT_PUBLIC_KEY_FILE:-}" ]]; then
    echo "GRPC_JWT_PUBLIC_KEY_FILE env is required"; exit 1
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
exec "${ROOT}/build/tts_server"