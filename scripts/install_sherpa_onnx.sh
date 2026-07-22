#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="${ROOT}/deps"
TAR="sherpa-onnx-v1.13.4-linux-x64-shared.tar.bz2"
URL="https://github.com/k2-fsa/sherpa-onnx/releases/download/v1.13.4/${TAR}"

mkdir -p "${DEPS}"
cd "${DEPS}"

if [[ -d "sherpa-onnx-v1.13.4" ]]; then
    echo "sherpa-onnx-v1.13.4 already extracted"
    exit 0
fi

if [[ ! -f "${TAR}" ]]; then
    echo "downloading ${TAR}"
    curl -fL --retry 3 -o "${TAR}" "${URL}"
fi

tar xjf "${TAR}"
mv sherpa-onnx-v1.13.4-linux-x64-shared sherpa-onnx-v1.13.4
echo "extracted to ${DEPS}/sherpa-onnx-v1.13.4"

if [[ -f "${ROOT}/scripts/install_sherpa_onnx_headers.py" ]]; then
    python3 "${ROOT}/scripts/install_sherpa_onnx_headers.py" || true
fi

echo "lib: ${DEPS}/sherpa-onnx-v1.13.4/lib"
echo "include: ${DEPS}/sherpa-onnx-v1.13.4/include"