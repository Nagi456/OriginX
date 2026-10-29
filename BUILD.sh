#!/usr/bin/env bash
# これを実行するだけで、HUD(MyClient.exe)をビルドして、配布用zipまで作ります。
#   bash BUILD.sh
# できあがり: MyClient-final.zip （これをダウンロードして使います）
set -euo pipefail
cd "$(dirname "$0")"
ROOT="$PWD"
CXX="${CXX:-x86_64-w64-mingw32-g++}"

if ! command -v "$CXX" >/dev/null 2>&1; then
  echo "[0/3] MinGW-w64 を入れています..."
  sudo apt-get update
  sudo apt-get install -y mingw-w64 g++-mingw-w64-x86-64-posix
fi

echo "[1/3] HUD (MyClient.exe) をビルドしています..."
cd source
mkdir -p build
$CXX -std=c++17 -O2 -Wall -Wextra -Wno-unknown-pragmas \
  -DNOMINMAX -D_WIN32_WINNT=0x0601 -static -static-libgcc -static-libstdc++ \
  MyClient.cpp MinecraftDataProvider.cpp -o build/MyClient.exe \
  -mwindows -luser32 -lgdi32 -lshell32
cp build/MyClient.exe "$ROOT/MyClient.exe"
cd "$ROOT"

echo "[2/3] SHA256SUMS.txt を更新しています..."
python3 - <<'PY'
import hashlib, os
names = [l.split('  ', 1)[1].rstrip('\n') for l in open('SHA256SUMS.txt', encoding='utf-8') if '  ' in l]
out = [hashlib.sha256(open(f, 'rb').read()).hexdigest() + '  ' + f
       for f in sorted(set(names)) if os.path.exists(f)]
open('SHA256SUMS.txt', 'w', encoding='utf-8').write('\n'.join(out) + '\n')
PY

echo "[3/3] 配布用zipを作っています..."
rm -f MyClient-final.zip
zip -q -r MyClient-final.zip . -x ".git/*" "source/build/*" "MyClient-final.zip" "BUILD.sh" "BUILD-README.txt"

echo
echo "完了: $ROOT/MyClient-final.zip"
ls -la MyClient.exe MyClient-final.zip
echo "左のファイル一覧で MyClient-final.zip を右クリック →「Download」してください。"
