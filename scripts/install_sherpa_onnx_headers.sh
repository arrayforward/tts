#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="${ROOT}/deps"
DEST="${DEPS}/sherpa-onnx-v1.13.4/include"

if [[ ! -d "${DEST}" ]]; then
    echo "missing ${DEST}; run scripts/install_sherpa_onnx.sh first"
    exit 1
fi

REPO="k2-fsa/sherpa-onnx"
TAG="v1.13.4"
BASE="https://raw.githubusercontent.com/${REPO}/${TAG}/sherpa-onnx"

mkdir -p "${DEST}/sherpa-onnx/csrc"

# Core TTS headers needed for the C++ class
HEADERS=(
    "sherpa-onnx/csrc/offline-tts.h"
    "sherpa-onnx/csrc/offline-tts-model-config.h"
    "sherpa-onnx/csrc/offline-tts-model-config-impl.h"
    "sherpa-onnx/csrc/macros.h"
    "sherpa-onnx/csrc/parse-options.h"
)

for h in "${HEADERS[@]}"; do
    rel="${h#sherpa-onnx/}"
    out="${DEST}/${rel}"
    mkdir -p "$(dirname "${out}")"
    if [[ ! -f "${out}" ]]; then
        echo "fetching ${h}"
        curl -fsL "${BASE}/${h}" -o "${out}" || {
            echo "WARN: failed to fetch ${h}" >&2
        }
    fi
done

echo "headers in ${DEST}"
find "${DEST}" -name '*.h' | wc -l