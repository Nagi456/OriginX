#!/usr/bin/env bash
# Builds signer/OriginXSigner.exe (the author's signing tool, docs/signing.md) with MinGW-w64, and runs its tests.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
g++ -std=c++17 -O2 -Wall -Wextra -Werror -I. -Ilauncher/vendor tests/signer_core.cpp -o build/signer-core-test
build/signer-core-test
x86_64-w64-mingw32-g++-posix -std=c++17 -O2 -Wall -Wextra -Wno-unknown-pragmas -DNOMINMAX -D_WIN32_WINNT=0x0601 -municode \
  -Ilauncher/vendor -I. signer/OriginXSigner.cpp -o build/OriginXSigner.exe -static -s -lbcrypt
sha256sum build/OriginXSigner.exe
