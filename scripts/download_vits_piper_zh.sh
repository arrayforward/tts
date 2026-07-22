#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MODEL_DIR="${TTS_LOCAL_MODEL_DIR:-${ROOT}/models/vits-piper-zh_CN-huayan-medium}"
TAR="vits-piper-zh_CN-huayan-medium.tar.bz2"
URL="https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/${TAR}"

mkdir -p "$(dirname "${MODEL_DIR}")"
cd "$(dirname "${MODEL_DIR}")"

if [[ -f "${MODEL_DIR}/model.onnx" && -f "${MODEL_DIR}/tokens.txt" ]]; then
    echo "model already present at ${MODEL_DIR}"
    exit 0
fi

if [[ ! -f "${TAR}" ]]; then
    echo "downloading ${TAR}"
    curl -fL --retry 3 -o "${TAR}" "${URL}"
fi

mkdir -p "${MODEL_DIR}"
tar xjf "${TAR}" -C "${MODEL_DIR}" --strip-components=1
[[ -f "${MODEL_DIR}/model.onnx" ]] || mv "${MODEL_DIR}/zh_CN-huayan-medium.onnx" "${MODEL_DIR}/model.onnx"
echo "extracted to ${MODEL_DIR}"
ls -la "${MODEL_DIR}"