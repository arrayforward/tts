#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOG_DIR="${TTS_LOG_DIR:-${ROOT}/logs}"
mkdir -p "${LOG_DIR}"

if [[ -z "${MINIMAX_API_KEY:-}" ]]; then
    echo "MINIMAX_API_KEY env is required"; exit 1
fi
if [[ -z "${GRPC_JWT_PUBLIC_KEY_FILE:-}" ]]; then
    echo "GRPC_JWT_PUBLIC_KEY_FILE env is required"; exit 1
fi

nohup "${ROOT}/build/tts_server" \
    >"${LOG_DIR}/tts_server.out" 2>"${LOG_DIR}/tts_server.err" &
echo $! > "${LOG_DIR}/tts_server.pid"
echo "started pid=$(cat "${LOG_DIR}/tts_server.pid"), logs in ${LOG_DIR}"