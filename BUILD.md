# BUILD.md

Windows x64 で DLL（`MyClientData.dll`）と HUD・ランチャー・ブリッジを作る手順。`build.ps1` は
`source/tools/build_client.py` と `source/build.sh` と同じコマンドを順に実行する。

ビルド構成は 1 つだけ（x64、`/O2 /DNDEBUG`、`/MD`）。Debug 構成は無い。
Linux・クラウドでのビルドは [docs/release.md](docs/release.md)「クラウドでのビルド」。

## 前提ツール
| 用途 | 必要なもの |
|---|---|
| DLL | Python 3、LLVM（`clang-cl`・`lld-link`・`llvm-readobj`・`llvm-dlltool` が PATH に）、MSVC の headers/libs（Visual Studio の開発者プロンプト、または xwin の `--msvc-root`） |
| DLL の SDK | 初回に GitHub から取得（ネットワークが要る。固定は `source/tools/client-sdk.lock.json`・`source/tools/sdks/<版>.lock.json`） |
| HUD・ランチャー | MinGW-w64（`x86_64-w64-mingw32-g++`・`x86_64-w64-mingw32-windres` が PATH に。別の g++ は `-Cxx` か `$env:CXX`） |

## build.ps1 の使い方
リポジトリ直下で、開発者 PowerShell（x64）から実行する。

```powershell
.\build.ps1 -Standalone                         # 26.51.1・26.60.5・26.45.1 の単独版 DLL ＋ HUD・ランチャー
.\build.ps1 -Standalone -Games 26.51.1 -SkipHud # DLL を 1 版だけ
.\build.ps1 -SkipDll                            # HUD・ランチャーだけ
.\build.ps1 -Standalone -MsvcRoot C:\xwin\msvc  # 開発者プロンプトの外（xwin の splat）
.\build.ps1 -Standalone -AllowStlMismatch       # STL1000 で止まるとき
```

| 引数 | 意味 |
|---|---|
| `-Games` | DLL の版（既定 `26.51.1`,`26.60.5`,`26.45.1`。26.45.1 は単独版のみ） |
| `-Standalone` | LeviLamina なしの DLL（無しなら LeviLamina 版） |
| `-MsvcRoot` / `-SdkRoot` | MSVC の場所 / SDK の置き場所 |
| `-SkipDll` / `-SkipHud` | どちらかを省く |
| `-AllowStlMismatch` | `CL=/D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH` をこの実行だけ設定 |
| `-Python` / `-Cxx` | 使うコマンド |

- 出力：`source\build\client[-<版>][-standalone]\MyClientData.dll`、`source\build\MyClient.exe`・`MyClientLauncher.exe`・`MyClientBridge.dll`。
- 全出力は省略せず画面に出て、`source\build\build.log` にも残る。最後に各手順の OK / FAILED と `BUILD SUCCEEDED` / `BUILD FAILED` が出る。失敗の箇所は直前の出力。終了コードは成功のとき 0。
- 実行前に `source/build/`・`client/GameRvaTable.inc`・`client/GameSigs.inc` を手で直さない（生成物。`make_sigs.py` が作る）。

**注意（未検証）**：`build.ps1` はこの環境（Linux）では PowerShell が無く、構文の確認も実行もしていない。最初の実行で問題があれば `build.log` を添えて報告する。

## よくある失敗と対処
| 症状 | 原因・対処 |
|---|---|
| `Command not found: clang-cl`（など） | LLVM を入れて PATH に通す。Linux では `/usr/bin/clang-cl-18` を `clang-cl` で symlink |
| `STL1000`（`static_assert`、Clang のバージョン不足） | LLVM を新しくするか `-AllowStlMismatch`（ソースは変えない） |
| `cannot open input file 'kernel32.lib'`・`'type_traits' file not found`（推測：実際には未確認） | MSVC の INCLUDE/LIB が無い。開発者プロンプトで実行するか `-MsvcRoot` を渡す |
| `HTTP Error 403`（SDK の取得、`codeload.github.com`） | プロキシ・ネットワーク制限。制限の無い環境で実行する（クラウドの回避手順は docs/release.md） |
| `tools/sigs.json has problems` | sigs.json の誤り。表示された項目を直す。`GameRvaTable.inc`・`GameSigs.inc` を手で直さない |
| `Compile failed: <ファイル>` | その前の出力に全ファイルのエラーが出ている（`-ferror-limit=0`） |
| `The standalone DLL still imports LeviLamina` | `-Standalone` の DLL が LeviLamina を参照している。`client/Standalone.cpp` の分岐と import を確認 |
| `Borrowed SDK layouts require --standalone` | 26.45.1 は `-Standalone` が必須 |
| `Windows x64 MinGW-w64 compiler required` | `-dumpmachine` が `x86_64-w64-mingw32*` でない。MinGW-w64 の g++ を `-Cxx` で指定 |
