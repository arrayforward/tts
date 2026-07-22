#!/usr/bin/env bash
set -euo pipefail

DIR="$(cd "$(dirname "$0")/.." && pwd)/keys"
mkdir -p "${DIR}"

openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out "${DIR}/jwt_private.pem" 2>/dev/null
openssl rsa -in "${DIR}/jwt_private.pem" -pubout -out "${DIR}/jwt_public.pem" 2>/dev/null
chmod 600 "${DIR}/jwt_private.pem"

cat >"${DIR}/mint_token.py" <<'PY'
import base64
import hashlib
import hmac
import json
import sys
import time

with open(sys.argv[1], "r") as f:
    private_pem = f.read()

def b64url(b):
    return base64.urlsafe_b64encode(b).rstrip(b"=").decode()

header = {"alg": "RS256", "typ": "JWT"}
payload = {"iss": "tts-service", "sub": "tester", "exp": int(time.time()) + 3600}
h = b64url(json.dumps(header, separators=(",", ":")).encode())
p = b64url(json.dumps(payload, separators=(",", ":")).encode())
signing_input = (h + "." + p).encode()

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding
key = serialization.load_pem_private_key(private_pem.encode(), password=None)
sig = key.sign(signing_input, padding.PKCS1v15(), hashes.SHA256())
print(h + "." + p + "." + b64url(sig))
PY

echo "keys written to ${DIR}/"
echo "public:  ${DIR}/jwt_public.pem"
echo "private: ${DIR}/jwt_private.pem"
echo "use GRPC_JWT_PUBLIC_KEY_FILE=${DIR}/jwt_public.pem"
echo "mint a test token: python3 ${DIR}/mint_token.py ${DIR}/jwt_private.pem"