#!/usr/bin/env bash
set -euo pipefail

PROTO_DIR="$(cd "$(dirname "$0")/.." && pwd)/proto"
GEN_DIR="$(cd "$(dirname "$0")/.." && pwd)/src/pb2"
mkdir -p "${GEN_DIR}"

if ! command -v protoc >/dev/null; then
    echo "protoc not found. run scripts/install_deps.sh first."
    exit 1
fi
if ! command -v grpc_cpp_plugin >/dev/null; then
    echo "grpc_cpp_plugin not found. run scripts/install_deps.sh first."
    exit 1
fi

protoc \
    --cpp_out="${GEN_DIR}" \
    --grpc_out="${GEN_DIR}" \
    --plugin=protoc-gen-grpc="$(command -v grpc_cpp_plugin)" \
    -I "${PROTO_DIR}" \
    "${PROTO_DIR}/tts.proto" \
    "${PROTO_DIR}/mediator_tts.proto"

echo "proto generated in ${GEN_DIR}"