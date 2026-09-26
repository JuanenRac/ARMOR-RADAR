<p align="center">
  <img src="images/ARMOR_BANNER.svg" alt="ARMOR-RADAR banner" width="100%">
</p>

# 📡 ARMOR-RADAR

<p align="center">
  <a href="README.md">🇺🇸 English</a> |
  <a href="README_spa.md">🇪🇸 Español</a> |
  <a href="README_fra.md">🇫🇷 Français</a> |
  <a href="README_ita.md">🇮🇹 Italiano</a> |
  <a href="README_deu.md">🇩🇪 Deutsch</a> |
  <a href="README_zho.md">🇨🇳 简体中文</a> |
  🇯🇵 <b>日本語</b>
</p>

### フィールドノードのファームウェア（Waveshare ESP32-S3-ETH、レーダーまたは人感センサー 3 基、イーサネットと Wi-Fi）。独自の Web パネルと、コンピューターでテスト済みのコア付き

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Target-ESP32--S3-e7352c.svg" alt="Target">
  <img src="https://img.shields.io/badge/Maturity-scaffolding-FFB020.svg" alt="Maturity">
</p>

---

**正直さのチェック - 今日動いているもの:** **成熟度：scaffolding。** ハードウェア非依存のコア（859 件のチェック：LD2450、LD2461、4 種の人感センサーのデコーダー、LD2450 のコマンドチャネル、設定とその検証、ピン表、ユーザーとセッション、割り当てピンのロジック、ネットワーク計画、そして ARMOR-COMMON が受け入れる出力を持つメッセージシリアライザー）はコンピューターでテスト済みで、**Web パネル**は代役ノードに対して実際のブラウザーで試し、**ファームウェアイメージ**は ESP-IDF 5.4.2 コンテナーでビルドできます。**ボード上で動いたことはありません**：実モジュールからフレームを取得したことはなく、イーサネット、Wi-Fi ブリッジ、光センサー、更新のコードは未試験で、レーダーのコマンドチャネルはメーカー文書とも実モジュールとも照合しておらず、パネルの HTTPS 証明書はノード自身が作るため実ブラウザーでは未試験で、LD2461 と人感センサーのデコーダーは各マニュアルの例としか一致せず、実モジュールはありません。

---

## 🎯 概要

* **2 種類のボード、1 つのファームウェア：** Waveshare ESP32-S3-ETH（Ethernet、標準）と、Ethernet のない ESP32-S3-WROOM-1 N16R8（Wi-Fi のみ：セットアップで参加する Wi-Fi ネットワークを尋ね、ノードは常に独自のネットワークも保ちます）。イメージはビルド時に選びます（`tools/build_node.sh generic s3-eth` または `generic s3-wifi`）。ピン表とアクセス方法はボードに従い、パネルはボードにないものを隠し、イメージは対応するボード専用です。どちらもビルドでき、コンピューター上でテスト済みですが、実機では動かしていません。
* **270 度のノードが 2 つ：** 各 Waveshare ESP32-S3-ETH は 3 本の UART で最大 3 基の LD2450 レーダーを読み取ります。レーダーは 75 度ずつずらして取り付け、有線イーサネット（W5500）で DHCP または固定アドレス、給電は PoE か USB です。Studio の *Add a 270° node* は、ノードに割り当て済みの 3 基のレーダーを作成します。
* **すべてのノードに Web パネル：** Studio と同じ見た目で 7 言語に対応し、ファームウェアに組み込まれています。概要、ネットワーク、Wi-Fi、ブローカー、レーダー、ピン、ユーザー、ファームウェア更新、ログ。ユーザーのいないノードは Wi-Fi `ARMOR-SETUP-xxxxxx` を開き、セットアップコードで最初の管理者を作成します。パスワードはソルト付き PBKDF2、セッションはランダムなトークン、すべての設定はノードのフラッシュにあるので、1 つのイメージがすべてのノードに使え、パスワードはコンパイルされません（[パネル](docs/NODE_PANEL.md)）。
* **複数のノードで 1 つの Wi-Fi：** 各ノードはイーサネットポートにつながるアクセスポイントを提供できます。同じ名前とパスワードにしてチャネルを自動（MAC により 1、6、11）にすると、スマートフォンと Wi-Fi センサーには 1 つの DHCP サーバーを持つ 1 つのネットワークに見えます。無線メッシュではなく、各ノードはケーブルを保ちます。代わりにルーターの Wi-Fi に参加でき（ネットワーク検索付き）、ケーブルのないノードは Android アプリから **Bluetooth** で設定できます。
* **サーバー用のピン：** 空いているピンは入力、出力（ブローカーを失ったときの安全な状態付き）、PWM、アナログ読み取りになり、サーバーのデバイスとして現れるので、リレーや接点に新しいファームウェアは要りません。ボードの予約ピンは提示されません。
* **ロールバック付きのオーバーザエア更新**をパネルから行えます（16 MB のフラッシュに 2 スロット）。また Studio から各ノードのパネルへ、ノードが公開するアドレスを使う**リンク**があります。
* **パネルの HTTPS：** ノードは自分の証明書（自己署名、フラッシュに保存）を作り、80 番に加えて 443 番、または 443 番のみでパネルを提供します。セッションクッキーは Secure が付き、ブラウザーの警告と照合できるよう証明書のフィンガープリントを表示します。**安定したトラック ID：** 各フレームのターゲットは以前追跡していたものと対応づけられるため、人は同じトラック ID を保ち、フレーム欠落でちらつかず、位置は少し平滑化されます。
* **センサーモデルは 6 種類、ポートごとに 1 つ：** ポートには LD2450 または LD2461（周辺を担うトラッカーで、Studio にモデル固有の視野あり）か、人感センサー（LD2410、LD2412、LD2410S、MR24HPC1）を接続でき、後者はサーバーのデバイスになって在・不在と距離を公開します。パネルで選びます。各センサーの説明、プロトコル、未検証の点は `docs/SENSORS.md` にあります。
* **LD2450 のデコーダー、状態、設定：** 再同期するフレーム検出器がノイズの多いストリームからフレームを見つけ、30 バイトの各フレームから最大 3 つのターゲットが契約のトラックになります。コンソールとパネルはレーダーごとに、報告中か、沈黙か、文字化けかを示します。パネルはモジュールのバージョンを読み、1 つまたは 3 つのターゲットを選び、検出ゾーンを設定することもできます（モジュールでは未検証のプロトコル）。
* **周囲光と契約に厳密なメッセージ：** 自動レンジの VEML7700。公開スキーマに従い、無効なものは書き出さないテレメトリ・ヘルス・情報の JSON、壁時計のタイムスタンプ（SNTP）、MQTT の遺言。どのレーダーも報告していない間はテレメトリを控え、空の「異常なし」は決して送りません。
* **ネットワーク製品のように、全ボード共通の 1 つのイメージ：** ファームウェアは同一で、MAC がボードを区別します（名前を付けるまでは `armor-` と 6 桁）。`adopt_node.py` は、書き込んだばかりのノードにネットワーク経由で管理者、ブローカーの ID、フリート共通の設定を与えます。セットアップコードは MAC から計算されるので、5 台でも 27 台でも作業は同じです。**ベンチツール：** USB-C での書き込み、パネル開発用の代役ノード、生フレームのログをテスト用フィクスチャに変換するコンバーター（[ベンチでの立ち上げ](docs/BENCH_BRINGUP.md)）。

## 📂 リポジトリの構成

```text
ARMOR-RADAR/
├── main/
│   ├── app_main.cpp        start-up: settings, radars, pins, network, panel, broker
│   ├── node_store.cpp      settings and users in flash
│   ├── network.cpp         Ethernet, Wi-Fi access point and bridge, station
│   ├── web_server.cpp      the panel and its JSON API, login, update
│   ├── radar_manager.cpp   UARTs, frames, health, command channel
│   ├── gpio_manager.cpp    the pins mapped for the server
│   ├── mqtt_link.cpp       clock, health, telemetry, information, pin topics
│   ├── board_ethernet.cpp, light_sensor.cpp, log_buffer.cpp, entropy.cpp
│   ├── Kconfig.projbuild   the first settings of a build
│   └── core/               framer, ld2450, ld2450_command, node_config, board_pins, network_plan, auth, gpio_logic, json, veml7700, telemetry_json...
├── panel/                  index.html, app.js, text.js (7 languages), style.css
├── tests/                  test_core.cpp, test_node.cpp, emit_samples.cpp, check_contract.py, test_tools.py
├── tools/                  build_node.sh, make_fleet.py, adopt_node.py, provision_node.sh, flash.bat, pack_panel.py, panel_mock.mjs, frames_to_fixture.py
├── secrets/                node.conf.example, fleet.example.json (the real files are git-ignored)
├── partitions.csv, sdkconfig.defaults
└── docs/                   BENCH_BRINGUP.md, NODE_PANEL.md, HARDWARE_BOUNDARY.md
```

## 🛠️ 開発環境

```bash
cmake -S tests -B build/host && cmake --build build/host
build/host/test_core && build/host/test_node && build/host/test_sensors && build/host/test_board_wifi   # 859 checks, -Werror
build/host/emit_samples | python tests/check_contract.py
node tools/panel_mock.mjs --user admin:adminpass123   # the panel without a board
python tools/make_fleet.py                             # once: the fleet secret and the shared settings
tools/build_node.sh generic                           # ONE image for every Waveshare board (dist/generic-s3-eth.bin), in the ESP-IDF container
tools/build_node.sh generic s3-wifi                   # the same firmware for an ESP32-S3-WROOM-1 N16R8 with no Ethernet
```

```bat
tools\flash.bat generic COM5 monitor                  # each Waveshare board, once, by USB-C (add s3-wifi for the other board)
```

```bash
tools/adopt_node.py 192.168.0.181 --id perimetro-3 --fleet secrets/fleet.json --broker-ssh-host <cm5> --broker-ssh-user <user>
```

ホストテストには C++17 コンパイラー（Linux、WSL、MSYS2）が必要です。[ベンチでの立ち上げ](docs/BENCH_BRINGUP.md)、[パネル](docs/NODE_PANEL.md)、[ハードウェアの境界](docs/HARDWARE_BOUNDARY.md)を参照。

## 🔗 関連プロジェクト

**A.R.M.O.R.**（Autonomous Radar & Multimodal Observation Range）は、独立したリポジトリで構成される周辺警備システムです。それぞれに独自のバージョン、テスト、README があります。ファミリーは次のとおりです：

* **[ARMOR-COMMON](../ARMOR-COMMON)** - メッセージ契約、検証器、適合性ベクトル、生成された型
* **ARMOR-RADAR** (このリポジトリ) - ESP32-S3 用フィールドノードのファームウェア。レーダー 3 基と独自の Web パネル付き
* **[ARMOR-SOLAR](../ARMOR-SOLAR)** - 太陽光インバーターとバッテリーのプロトコル、およびゲートウェイノードのメッセージ
* **[ARMOR-SERVER](../ARMOR-SERVER)** - 中央コーディネーター：テレメトリ、アラーム、デバイス、太陽光の測定値、カメラ
* **[ARMOR-STUDIO](../ARMOR-STUDIO)** - Web コンソール：カメラ、レーダー、アラーム、太陽光発電、2D/3D サイト設計
* **[ARMOR-ANDROID-CONTROL](../ARMOR-ANDROID-CONTROL)** - リアルタイム 2D/3D レーダー付きの Android オペレータークライアント
* **[ARMOR-SERVER-AI](../ARMOR-SERVER-AI)** - 判断を説明し、決して動作しない視覚推論ポリシー
* **[ARMOR-VOICE-AI](../ARMOR-VOICE-AI)** - 偽造できない確認を備えたオフライン音声インテント
* **[ARMOR-HARDWARE](../ARMOR-HARDWARE)** - 筐体、電子部品、ベンチ受け入れマトリクス
* **[ARMOR-DEVOPS](../ARMOR-DEVOPS)** - デプロイ、CM5 テストベンチ、バックアップ、TLS
* **[ARMOR-SIMULATOR](../ARMOR-SIMULATOR)** - 再現可能な故障を備えたオフラインのテレメトリシミュレーター
* **[ARMOR-DOCS](../ARMOR-DOCS)** - アーキテクチャ、セキュリティ基準、機能マトリクス

## 📚 ドキュメントとコミュニティ

詳しくは：

* [機能マトリクス：実証済みのものとそうでないもの](../ARMOR-DOCS/docs/CAPABILITY_MATRIX.md)
* [プロジェクト一覧：バージョンとリポジトリ間の依存関係](../ARMOR-DOCS/docs/PROJECT_CATALOG.md)
* [このリポジトリの変更履歴](CHANGELOG.md)
* [ライセンス（GPL-3.0-or-later）](LICENSE)
* 質問・提案・報告：electrohobby3d@gmail.com

## 👤 作者

**JuanenRac (Electro Hobby 3D)** · electrohobby3d@gmail.com

## 📜 ライセンス

GPL-3.0-or-later - [LICENSE](LICENSE) を参照。
