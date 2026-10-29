# MyClient — Zoom・FPS・防具・矢・Full Bright / Windows x64

Minecraft内で動く `MyClientData.dll`、外部HUDの `MyClient.exe`、導入・起動用の **`MyClientLauncher.exe`** です。DLLはLeviLamina ClientのMODとして読み込みます。

**この版はMyClientLauncherだけを操作して準備・起動します。** 初回にゲーム本体を専用フォルダーへコピーし、必要なローダーを公式配布元から取得します。そのコピーにDLLを配置してMinecraftを直接起動し、DLLへの接続を確認してHUDを起動します。

## 今回追加した機能と未完了の範囲

26.51.1用のDLLとHUDに、次のゲーム連携を追加しました。

- **Zoom**：GのメニューからONにして、初期設定のCキーを押している間、約3倍に拡大。ゲームが前面で、インベントリ・チャットなどのメニューを開いていないときだけ有効。キーを離すと元のFOV計算に戻ります。視野角設定ファイルは変更しません。
- **FPS**：Minecraftの`GameRenderer::renderCurrentFrame`の実行回数を約1秒ごとに平均。HUDの描画回数やゲームの20TPSをFPSとして表示しません。GPUが実際に提示したフレーム数の計測ではありません。取得前や停止後は`---`。
- **Armor HUD**：頭・胴・脚・足の装備の残り耐久値/最大値を取得。空の装備欄は`EMPTY`、耐久値のない装備は`N/A`、未取得は`---`。
- Zoom行のクリック/右クリック処理にあった配列範囲外参照を修正しました。

**コード・ビルド・合成テストを完了した段階です。Windows/Minecraftの実機動作は未確認です。** ゲーム関数は26.51.1のSDK宣言と公式ローダーのシンボル解決に基づきます。実行時の関数解決やフック登録に失敗した場合はログにエラーを残し、新しい3機能を有効化しません。矢・照準座標の取得は続けます。

Party Auto AcceptにHive用の招待検出と承認コマンド送信を追加しました。実際のHive接続での動作は未確認です。Block Outlineは照準先のブロック座標表示までで、3D枠は未実装です。

バージョン選択、起動時の更新一覧取得、対応DLLの自動取得・SHA256検査・版ごとの保存を追加しました。

| ゲーム版 | 状態 |
| --- | --- |
| 26.45.1 | SDK・DLLの対応待ち。起動不可 |
| 26.50.4 | SDK・DLLの対応待ち。起動不可 |
| 26.51.1 | 同梱DLLを利用可能。実機動作は未確認 |
| 26.52.3 | SDK・DLLの対応待ち。起動不可 |

**4版すべてで遊べる完成版ではありません。** 26.51.1以外のDLL、本体の自動ダウンロード・旧版導入、ワールド分離が未実装です。GitHubの更新ファイルも未公開のため、現時点では同梱26.51.1を使います。

一度この更新対応版を入れた後は、互換性のあるDLL修正や新しい版の対応ファイルを公開すると、次回の起動時に一覧へ反映され、「適用して起動」で取得できます。DLLの作成そのものは自動ではありません。ローダー方式やHUD通信形式の変更時にはランチャー更新が必要になる場合があります。

開発・公開手順は同梱の `UPDATES-ja.md`（ソースでは `launcher/UPDATES.md`）にあります。

## この版で実装したこと

- ゲームのクライアント更新イベントで、ローカルプレイヤーのインベントリを読む。
- メインインベントリ（ホットバーを含む）とオフハンドにある矢の個数を合計する。効果付きの矢も合計する。チェスト、シュルカーボックスやバンドルの中身は集計対象外。
- ゲームのヒット結果がブロックのとき、照準先の正確な整数座標を取得する。エンティティへの照準や空振りでは表示を消す。
- 「Block Outline」をONにすると、照準の横に `Block: x, y, z` を表示する。詳細画面にも座標と接続状態を表示する。
- ワールド退出時にデータを消し、更新が2秒途切れた場合もHUDの古い表示を消す。
- データが未取得なら矢は `---`。取得済みで0本の場合は `0`。

**この版の照準先表示は座標です。ブロック形状の3D枠の投影は含みません。** 元の枠描画インターフェースは残していますが、架空の枠データは生成しません。PartyはHive用の招待検出・承認コマンド送信を追加しました（実機未検証）。Zoom・FPS・防具は上記の連携実装を追加しました。

## Full Bright（追加機能・実機未検証）

Gのメニューの「Full Bright」をONにすると、ゲームが計算する暗視の明るさ（night vision scale）を1.0以上に保ち、洞窟や夜を暗視状態と同じ明るさで表示します。

- ゲームの設定ファイル（明るさ設定）や効果・ワールドの状態は書き換えません。ONの間、描画側が読む計算済みの値だけを引き上げます（Zoomと同じ「戻り値を調整する」方式）。
- HUDは250ms期限のリースを毎tick更新します。OFFにする、HUDを閉じる、HUDが落ちると、1秒以内に通常の明るさへ戻ります。
- フックが見つからない版では、Full Bright**だけ**がHUDで `UNAVAILABLE (game hook not found)` と表示され、Zoom・FPS・防具はそのまま動きます。
- 設定は `FULLBRIGHT <0|1>` の1行で保存します（古い設定ファイルは読み込み時にOFF扱い）。
- 通信形式 `Local\MyClient.Features` の内部版を 1 → 2 に上げました。**DLLとHUDは必ず同じ配布セットを使ってください。**（片方だけ古いと拡張機能の通信が成立しません。矢・照準座標のABI 2は影響を受けません。）

### Full Bright の実装（0.4.1）

存在しない関数 `LocalPlayer::getNightVisionScale` のフックをやめ、SDKに実在する光マップ生成 `BaseLightTextureImageBuilder::buildImage / buildImageSplit` をフックします。呼び出し中だけ `BaseLightData` の暗視フラグと明るさを引き上げ、呼び出し後に元へ戻します（ゲーム状態・設定は書き換えません）。

- マングル名はSDK宣言から作成。**ゲーム本体での実在は未確認**です。無い場合は `UNAVAILABLE (game hook not found)` になり、他の機能は動きます。
- The End（`TheEndLightTextureImageBuilder`が独自実装）では効きません。
- 確認方法：Minecraft起動後、`MyClientData` のログにフックの成否が出ます。HUDが `APPLIED` なら有効です。

## 必要な環境

| 項目 | 対応対象 |
| --- | --- |
| OS / CPU | Windows 10/11 / x64 |
| Minecraft | 導入済みの統合版 **26.51.1 / GDK Release**（内部系列 `1.26.51`） |
| MODローダー | **LeviLamina Client 26.51.6** と依存ファイル（MyClientが初回に取得） |
| Windows部品 | Gaming Services / GameInput、Windows標準のPowerShell 5.1 |
| ランタイム | Microsoft Visual C++ 2015–2022 再頒布可能パッケージ x64 |

LeviLaminaの[対応バージョン表](https://github.com/LiteLDev/LeviLamina/blob/v26.51.6/docs/main/contents/versions.md)に合わせています。26.52、Preview版、Server版、別バージョンのLeviLaminaではロード時の検査で取得処理を開始しません。

## 導入（PCが使えるときに行ってください）

1. Microsoft Store / Xboxアプリで導入済みの **Minecraft 26.51.1 GDK版**を使います。最初にXboxアプリからゲームを一度起動し、Minecraftを終了してください。ゲーム本体とMicrosoftアカウントの利用権は別途必要です。
2. `MyClient-26.51-features-x64.zip`を**すべて**書き込み可能なフォルダーに展開し、**`MyClientLauncher.exe`**を開きます。EXEだけを移動せず、同梱ファイルを一緒に置いてください。
3. バージョン欄で **26.51.1** を選びます。初回は「選択…」で、本体の`Minecraft.Windows.exe`を指定します。Xboxアプリのゲーム管理画面の「ファイル」「参照」などから、インストール先を確認できます。同じフォルダーに`MicrosoftGame.config`があるGDK版が対象です。
4. **「適用して起動」**を押します。初回はゲーム本体をコピーし、必要なローダーを取得・検査して準備します。インターネット接続と、ゲーム本体のコピー分の空き容量が必要です。準備が終わるとMinecraftを直接起動します。
5. DLLへの接続を確認するとHUDが自動で起動します。ワールドに入り、`G`でメニューを開き、Arrow Counter / Block OutlineをONにします。
6. 次回からは`MyClientLauncher.exe`を開いて**「適用して起動」**を押します。準備済みのゲームを使うので、元のインストール先を毎回選ぶ必要はありません。

MyClientの保存先：

```text
%LOCALAPPDATA%/MyClient/
  launcher.json                 選択した版と版ごとの本体の場所
  catalog.json                  保存済み更新一覧
  packages/<版>/<revision>/     検査済みDLLとローダー設定
  launcher.log                  ランチャーの処理ログ
  runtime-setup.log             初回の準備ログ
  cache/                        検査済みのローダー配布アーカイブ
  game/26.51.1/
    Minecraft.Windows.exe       自作ソフトから起動するコピー
    MyClient-original.exe       変更前の実行ファイル
    MyClient-game.json          専用コピーの準備情報
    MyClient-runtime.json       ローダー準備完了とファイル検査情報
    PreLoader.dll
    bedrock_runtime_data
    mods/LeviLamina/
    mods/MyClientData/
    MyClient-backups/            DLL更新時の以前のMOD
```

コピー元のゲーム本体は変更しません。元の環境のMODはコピーせず、自作ソフトの専用コピーに今回のDLLを導入します。ワールド保存の仕組みはMinecraft本体の標準動作を使い、この版にワールドを分離する機能はありません。

ゲームの版はGDKの`MicrosoftGame.config`とWindowsの実行ファイルバージョンで確認します。別の版、UWP/Preview版、別ランチャー用のDLLを読み込む本体は準備の対象外です。導入済みの26.51.1本体が必要で、Minecraftの購入・ダウンロード・旧版の自動導入には未対応です。選択欄にあるだけでは、その版で遊べることを意味しません。

### 必要なWindows部品

Xboxアプリから通常のMinecraftが起動する状態を先に用意してください。不足する場合は、Microsoftの次の配布先を使います。

- [Xboxアプリ](https://www.xbox.com/apps/xbox-app-for-pc)
- [Gaming Services](https://apps.microsoft.com/detail/9mwpm2cqnlhn)
- [Microsoft GameInput](https://www.microsoft.com/download/details.aspx?id=100591)
- [Visual C++再頒布可能パッケージ x64](https://aka.ms/vs/17/release/vc_redist.x64.exe)

MyClientのローダー準備にはWindows標準のPowerShellを内部で使います。追加のPython、Git、lipや別ランチャーをユーザーがインストールする必要はありません。

### 更新・復旧

- MyClientを更新するときはMinecraftを終了して、新しいZIPを展開してから再度「適用して起動」を押します。以前のMODをバックアップし、MOD設定を引き継ぎます。
- 読み込み済みDLLに接続できるゲームが起動中なら、DLLを入れ替えずHUDを起動します。DLLの動的アンロードには対応していません。
- ダウンロードはSHA256とサイズ、配置後のファイルもSHA256で検査します。中断した準備は次回起動時に再試行します。準備完了や接続が確認できるまでは「接続済み」と表示しません。
- 「ログを開く」はローダー準備ログを開きます。通常の起動処理ログは`launcher.log`、ゲームのログは専用ゲームフォルダーの`logs/`を確認してください。
- 本体のコピー自体が破損した場合は、Minecraftを終了して`game/26.51.1/`を別の場所へ退避し、公式インストール元を選択して準備し直してください。退避したMOD設定やバックアップは必要に応じて戻せます。

### HUDが表示されない場合

- ゲームのログに`MyClientData loaded`と`MyClientData enabled`が出ているか確認します。
- 初回準備が失敗した場合は`runtime-setup.log`を確認します。別バージョンの本体、不足するVC++ランタイム、ネットワークエラーは準備や起動を停止します。
- ランチャーは起動・接続を2分待ちます。ゲームが接続前に終了した場合は終了コードを表示します。
- HUDのBlock Outline詳細画面の接続状態を確認します。`DLL: not connected`は通信未接続、`waiting for game data`はワールドデータ待ち、`stale`は更新停止です。
- ランチャー経由ではMinecraftのプロセスIDを指定してHUDを起動します。HUDを直接開く場合は、先にMinecraftを起動し、ゲームを複数同時起動しないでください。

以前渡した通信だけの`MyClientBridge.dll`は導入不要です。DLLとHUDは通信形式v2の同じ配布セットを使ってください。

## ビルド方法

配布ZIP内でビルドする場合は、まず `source/` に移動してください。

### ランチャー・HUD・通信テスト用DLL

C++17対応のMinGW-w64 **x64** g++が必要です。

```bat
build.bat
```

Linuxでは次を使用します。

```sh
bash build.sh
```

`build/MyClientLauncher.exe`、`build/MyClient.exe`、開発用の `build/MyClientBridge.dll`、`build/bridge_smoke.exe` を生成します。JSONとTinyXML2のソース・ライセンスは`launcher/vendor/`に同梱しています。リポジトリ直下の既存 `MyClient` は過去のLinux実行ファイルです。

### ゲーム内MODのDLL

LeviLaminaとC++の文字列・メモリ管理を共有するため、**clang-cl + MSVC ABI**を使います。MinGWでゲーム内MODをビルドしないでください。

WindowsではPython 3.9以上、LLVM（clang-cl、lld-link、llvm-readobj、llvm-dlltool）、Visual Studio C++ Build ToolsとWindows SDKが必要です。VSの **x64 Native Tools Command Prompt** から実行します。

```bat
python tools\build_client.py
```

SDKと依存ヘッダーを公式配布元から取得し、`tools/client-sdk.lock.json` のSHA-256で検証します。生成先は `build/client/MyClientData.dll` と `build/client/manifest.json` です。SDKの取得・ビルドはゲームのインストール先を書き換えません。

LinuxではLLVM 21のclang-cl系ツールと、xwinで取得したMSVC CRT / Windows SDKを使えます。このタスクで使ったコマンドの形式は以下です。

```sh
python3 tools/build_client.py \
  --sdk-root /path/to/client-sdk \
  --msvc-root /path/to/xwin-output \
  --compiler clang-cl-21 --linker lld-link-21 \
  --readobj llvm-readobj-21 --dlltool llvm-dlltool-21
```

MSVC CRT 14.44、Windows SDK 10.0.26100、LLVM 21.1.8でクロスビルドしました。SDKはLeviLamina v26.51.6、コミット `32fcaa02baa38371b705358801c7d185c284233e` に固定しています。

## 実装の構成

- `launcher/Updates.h` / `DownloadWin.h`：配布一覧の互換性検査、HTTPS取得、ハッシュとサイズ検査、版ごとのDLLキャッシュ。
- `launcher/MyClientLauncher.cpp`：Windowsのファイル選択、初回コピーと準備、ゲームの直接起動、DLL接続確認、HUD起動。
- `launcher/PrepareRuntime.ps1` / `runtime.lock.json`：固定版依存ファイルの取得・配置・SHA256検査、公式PeEditorによる専用コピーのDLL読み込み設定。
- `launcher/RuntimeWin.h` / `PeImports.h`：本体の版、ファイルのSHA256、PEのDLL読み込み先、準備完了の検査。
- `launcher/Install.h`：GDK設定XMLの確認、ゲームコピー、Windows引数の引用処理、一時フォルダーを使ったMOD更新とバックアップ。
- `FeatureData.h` / `FeatureChannel.h`：FPS・防具・Zoomの拡張通信、期限切れ要求の検査。
- `client/FeatureAdapter.cpp`：描画フレーム計測、FOVの一時変更、防具耐久値の取得。
- `client/MyClientData.cpp`：LeviLaminaのロード・有効化・無効化、クライアント更新／退出イベント、ゲームデータ取得。
- `client/GameData.h`：アイテムIDによる矢の集計と照準先データの作成。
- `client/MemoryOperators.cpp`：SDKが要求するゲーム側アロケーターの使用。
- `MyClientBridge.cpp` / `BridgeChannel.h`：同一WindowsセッションのPID別共有メモリとMutex。
- `MinecraftDataProvider.cpp`：HUD側の受信、形式検証、古いデータの破棄。
- `BridgeApi.h`：固定幅の通信形式v2。ゲームポインターやSTLオブジェクトはプロセス間で渡しません。

矢・照準座標の共有メモリ名は `Local\MyClient.Bridge.v2.<Minecraft PID>`、Mutexは末尾に `.Lock` が付きます。取得処理はゲームイベント内で実行し、ワールドやプレイヤーへのポインターを次の更新に持ち越しません。Zoomは公式ローダーのフックAPIで計算済みFOVの戻り値を変更します。固定オフセットは使わず、HUDからゲームオブジェクトへ直接書き込みません。Zoom要求は別の共有メモリ `Local\MyClient.Features.v1.<Minecraft PID>` を通じ、250msで期限切れになります。FPS・防具・Zoom適用状態もこの拡張チャンネルで返します。既存の矢・照準座標のABI 2は維持しています。

## 検証状況

更新機能の確認：カタログの版・互換性・配布URL・パス・ハッシュ情報、古い更新の拒否、取得失敗時の保護、キャッシュ再利用、余分なDLL/リンクの拒否、配布リビジョンと一覧番号の更新を合成データで検査しています。通信・ハッシュのテストでは代替関数を使い、WindowsのWinHTTP/BCrypt実行は未確認です。

```sh
g++ -std=c++17 -Wall -Wextra -Werror -I. -Ilauncher/vendor tests/launcher_updates.cpp launcher/vendor/tinyxml2/tinyxml2.cpp -o /tmp/myclient-updates
/tmp/myclient-updates
python3 tests/update_package.py
```

- 未実施：新規フックの実行時解決、実際の視野角変化、実機FPS・防具表示、拡張通信のWindowsスモーク実行。合成テストとリンク成功から実機動作は保証できません。
- 成功：Windows x64のランチャー・MOD DLL・HUD・通信テスト用EXEのクロスビルド。
- 成功：GDK XML/バージョン/PEの検査、ゲームコピーと元ファイルの保持、MOD更新・バックアップ・コピー失敗時の保護、Windows引数とPIDのネイティブテスト。実際の一時フォルダーと合成ゲームファイルを使います。
- 成功：公式の依存アーカイブ6個のSHA256/サイズ/CRC、配置内容、SDKと同じローダー版、x64とPreLoader依存の静的検査。
- 成功：LinuxのPowerShell 7.4.6で、合成アーカイブの配置・繰り返し準備・完了情報・元ファイル保持・不正パス/リンク/重複/ハッシュの拒否を検査。PeEditorは代替プログラムを使っています。
- 成功：矢の合計、既知の0本、不明アイテム、整数ブロック座標、通信形式のネイティブテスト。
- **未実施：Windows上の画面/ファイル選択、Windows PowerShell 5.1による依存取得・実際のPeEditor実行、Minecraftの直接起動、ゲーム内データ・HUD接続の実機確認。** この開発環境にはWindows/Minecraftがありません。コードの実装・ビルド完了と実機での動作確認は区別しています。

ネイティブテスト：

```sh
g++ -std=c++17 -Wall -Wextra -Werror -I. tests/features.cpp -o /tmp/myclient-features
/tmp/myclient-features
g++ -std=c++17 -Wall -Wextra -Werror -I. tests/frame_validation.cpp -o /tmp/myclient-frame-validation
/tmp/myclient-frame-validation
g++ -std=c++17 -Wall -Wextra -Werror -I. tests/game_data.cpp -o /tmp/myclient-game-data
/tmp/myclient-game-data
g++ -std=c++17 -Wall -Wextra -Werror -I. -Ilauncher/vendor tests/launcher_install.cpp launcher/vendor/tinyxml2/tinyxml2.cpp -o /tmp/myclient-launcher-install
/tmp/myclient-launcher-install
```

配布ZIPの作成（SDK準備と両ビルドの完了後。更新用ファイルを先に作成）：

```sh
python3 tools/package_update.py --sdk-root /path/to/client-sdk --output /tmp/myclient-release-assets --catalog launcher/catalog.json --revision 2 --sequence 2
python3 tools/package_client.py --sdk-root /path/to/client-sdk --output /path/to/output
```

`MyClient-26.51-features-x64.zip`ひとつにランチャー、HUD、MOD、ライセンス、ビルド可能なソースをまとめます。更新配信用の個別ファイルは作業用フォルダーに生成し、利用者へ個別ダウンロードさせません。

Windowsの通信スモークテストは `build/bridge_smoke.exe` です。合成データを使うためMinecraftの動作検証にはなりません。SDKや依存ライブラリの出典・ライセンスは `THIRD_PARTY_NOTICES.md` を参照してください。

依存アーカイブの再検査（配布ZIPにはこれらのバイナリを含めません）：

```sh
python3 tools/verify_runtime_assets.py --download --readobj llvm-readobj
```

PowerShellテストは`tests/runtime_setup.ps1`です。LinuxのPowerShell 7では合成アーカイブとPeEditor代替プログラムを使い、配置・繰り返し準備・完了情報・不正パスの拒否を検査します。このテストは実際のWindows/Minecraftの起動確認にはなりません。

## Hive Party Auto Accept（26.51.1・実機未検証）

Party Auto Acceptの「Registered GameTags」に相手のゲームタグを登録し、機能をONにします。HUDが動いている間だけ、Hiveの接続先を確認し、登録した相手の招待に `/party accept "ゲームタグ"` を送ります。初期状態はOFFです。設定保存の仕組みは既存のままです。

- 認識する候補文: `ゲームタグからパーティへの招待が来ています`（「パーティー」、末尾の「。」も対応）、`GameTag has invited you to their party!` / `GameTag has invited you to join their party!`（末尾の `.` も対応）。Minecraftの標準色・書式コードを除いて文全体を照合します。
- **日本語はいただいた説明、英語は候補形式をもとにした実装です。Hiveの実際の表示文はまだ取得できていません。** 接頭辞、複数行、翻訳キー、JSON形式などは承認せず、対応調整が必要です。一般プレイヤーのチャット・囁きは対象外です。
- 接続先は `geo/as/eu/sg/ca/us/na.hivebedrock.network` または `hivebedrock.network` の完全一致のみ。IPアドレスしか取得できない場合などは自動承認しません。
- 登録は最大64人、各32文字まで。英数字・半角スペース・`_`・`-`・`#` のみ。無効な登録があれば承認を停止します。タグの大文字小文字は区別しません。
- 同じ相手は30秒間重複送信を防ぎ、全体でも2秒以内の連続送信を抑えます。招待は3秒で破棄。HUDの設定通信が1秒以上止まった場合、OFF、切断・ワールド退出時も承認しません。停止中の招待を後から承認しません。
- `accept command sent (unconfirmed)` は送信した状態で、参加成功を保証しません。`unknown invite format (ignored)` は非対応の文を検出して無視した状態です。DLLログには非対応形式と文字数を記録しますが、チャット本文は保存しません。
- 承認コマンドは [Hive公式コマンド一覧](https://support.playhive.com/commands/) で確認しました。スペースを含むタグの引用符処理、ゲーム内フック、実際の招待受信・参加はWindows/Hiveでの確認が残っています。対象は26.51.1のみです。

DLLが変わったため、ゲームを終了してから同梱のランチャーで起動してください。更新用の版はrevision3です。GitHubへの更新ファイル公開はまだ行っていないため、オンライン自動配信は始まっていません。

## 対応バージョン

MyClient は現在 **Minecraft 1.26.51（26.51.1、LeviLamina 26.51.6）だけ**に対応しています。作成途中のため、古い版（26.40.5 / 26.32.2）向けのビルドと同梱ファイルはやめました。ランチャーの版一覧は 26.51.1（対応）と 26.52.3（未対応: 対応するLeviLaminaが未公開）だけです。

### 版の切り替え（ランチャー）

- ランチャーの **Settings → Version** タブ（ホーム右上の Versions、左下の版でも開く）に、公開されている版（LeviLauncherと同じ公開データ [minecraft-windows-gdk-version-db](https://github.com/LiteLDev/minecraft-windows-gdk-version-db)）、MyClientの対応（DLLの有無）、このPCにある版を一覧で表示します。一覧はオフライン用に `%LOCALAPPDATA%\MyClient\versions_db.json` に保存します。
- このPCの版は XboxGames・インストール済みパッケージ・**LeviLauncherの版フォルダー**（`%APPDATA%\levilauncher.exe\versions`、設定の base_root も）・Settings → General の「フォルダーを追加…」で追加したフォルダーから探します。LeviLauncherの版は、LeviLauncherが残す元の `Minecraft.Windows.exe.orig` を `game/<版>/` にコピーして使います（LeviLauncher側のフォルダーは変更しません）。
- ゲーム本体のダウンロード・展開はしません（LeviLauncherのダウンロードは暗号化されたパッケージを独自のライセンス処理で展開するもので、MyClientには組み込みません）。古い版はLeviLauncherやXboxアプリで入れてください。
- 遊べるのは、MyClientのDLLがあり（catalog.json で available）、このPCにある版だけです。

- SDK のロックは `tools/client-sdk.lock.json` です。
- 新しい版に移るときは `python tools/generate_sdk_lock.py --game <版> --tag <LeviLaminaのタグ>` でロックを作り、`tools/client-sdk.lock.json` を置き換えます。
- **LeviLaminaのクライアント版が存在する系列のみ**対応できます（26.45.x / 26.50.x / 26.52.x は現時点で存在しません）。
