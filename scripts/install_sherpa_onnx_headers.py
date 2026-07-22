#!/usr/bin/env python3
import os, re, sys, urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEST = os.path.join(ROOT, "deps", "sherpa-onnx-v1.13.4", "include")

if not os.path.isdir(DEST):
    print(f"missing {DEST}; run scripts/install_sherpa_onnx.sh first")
    sys.exit(1)

BASE = "https://raw.githubusercontent.com/k2-fsa/sherpa-onnx/v1.13.4/sherpa-onnx/csrc"
CSRC = os.path.join(DEST, "sherpa-onnx", "csrc")
os.makedirs(CSRC, exist_ok=True)

SEEN = set()
TODO = [
    "offline-tts.h",
    "offline-tts-model-config.h",
    "macros.h",
    "parse-options.h",
]

INCLUDE_RE = re.compile(r'^\s*#include\s+"([^"]+)"', re.MULTILINE)

def fetch(rel):
    name = os.path.basename(rel)
    out = os.path.join(CSRC, rel)
    if rel in SEEN or os.path.exists(out):
        SEEN.add(rel)
        return
    url = f"{BASE}/{rel}"
    print(f"fetching {rel}")
    try:
        with urllib.request.urlopen(url, timeout=20) as r:
            data = r.read()
    except Exception as e:
        print(f"WARN: failed to fetch {rel}: {e}")
        return
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "wb") as f:
        f.write(data)
    SEEN.add(rel)
    src = data.decode("utf-8", errors="replace")
    for m in INCLUDE_RE.finditer(src):
        inc = m.group(1)
        if inc.startswith("sherpa-onnx/csrc/"):
            fetch(inc[len("sherpa-onnx/csrc/"):])

while TODO:
    rel = TODO.pop(0)
    fetch(rel)

n = sum(1 for r, _, files in os.walk(CSRC) for f in files if f.endswith(".h"))
print(f"headers in {CSRC}: {n}")