#!/usr/bin/env bash
# One command to build MyClientData.dll (per game version) and the HUD in a Claude Code cloud container (MyClient-full).
#   bash source/tools/cloud_build.sh [--games "26.51.1 26.60.5 26.45.1"] [--no-standalone] [--skip-hud]
# Idempotent: what is already installed/cached is skipped. Prints OK or FAILED last. Output: source/build/client[-<game>]-standalone/MyClientData.dll,
# source/build/MyClient.exe. Never publishes.
# Why each step (found 2026-11-16): the container has clang-cl-18 but no `clang-cl`; codeload.github.com is blocked (403) but
# `git clone` works -> cloud_build_prep.py clones the SDK and skips the download; MSVC comes from xwin (cargo install, then splat);
# MSVC's STL asks for Clang 19, so /D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH goes in via the CL variable; the HUD needs MinGW (apt).
set -uo pipefail
games="26.51.1 26.60.5 26.45.1"; standalone="--standalone"; hud=1
while [ $# -gt 0 ]; do case "$1" in
  --games) games="$2"; shift;; --no-standalone) standalone="";; --skip-hud) hud=0;;
  *) echo "unknown argument: $1" >&2; exit 2;; esac; shift; done
src="$(cd "$(dirname "$0")/.." && pwd)"
cache="${MYCLIENT_CLOUD_CACHE:-$HOME/.cache/myclient-cloud}"
mkdir -p "$cache/bin"
fail() { echo "FAILED: $*" >&2; exit 1; }
step() { echo "== $*"; }

step "clang-cl"
if ! command -v clang-cl >/dev/null; then
  c="$(command -v clang-cl-18 || ls /usr/bin/clang-cl-* 2>/dev/null | head -1)"; [ -n "$c" ] || fail "no clang-cl-NN in /usr/bin"
  ln -sf "$c" "$cache/bin/clang-cl"
fi
export PATH="$cache/bin:$PATH"
command -v lld-link >/dev/null && command -v llvm-readobj >/dev/null && command -v llvm-dlltool >/dev/null || fail "lld-link/llvm tools missing"

step "xwin + MSVC"
if [ ! -x "$cache/xw/bin/xwin" ]; then cargo install xwin --locked --root "$cache/xw" || fail "cargo install xwin"; fi
if [ ! -d "$cache/msvc/crt/include" ]; then
  "$cache/xw/bin/xwin" --http-retry 6 --accept-license --cache-dir "$cache/xwc" splat --output "$cache/msvc" || fail "xwin splat"
fi

if [ "$hud" = 1 ]; then
  step "MinGW"
  if ! command -v x86_64-w64-mingw32-g++ >/dev/null; then
    { apt-get update -q && apt-get install -y -q g++-mingw-w64-x86-64 binutils-mingw-w64-x86-64; } >/dev/null || fail "apt install MinGW"
  fi
  step "HUD"
  (cd "$src" && bash build.sh) || fail "HUD build (build.sh)"
  sha256sum "$src/build/MyClient.exe"
fi

export CL="/D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH"
for g in $games; do
  if [ "$g" = "26.45.1" ] && [ -z "$standalone" ]; then step "DLL $g: skipped (standalone only; borrowed SDK layouts)"; continue; fi
  step "DLL $g"
  (cd "$src" && python3 tools/cloud_build_prep.py "$g" $standalone --msvc-root "$cache/msvc") || fail "DLL $g"
  d="build/client"; [ "$g" != "26.51.1" ] && d="build/client-$g"; [ -n "$standalone" ] && d="$d-standalone"
  sha256sum "$src/$d/MyClientData.dll"
done
echo OK
