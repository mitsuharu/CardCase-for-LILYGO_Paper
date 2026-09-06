# AGENTS.md

このリポジトリで作業するときの開発ルールをまとめる。人間もエージェントもここを参照する。

## プロジェクト概要

LILYGO T5 4.7 inch e-Paper（ESP32-S3）向けのアプリ。SD カードに保存した画像、または WiFi
でスマホから受け取った画像を一覧から選び、全画面に表示して名刺のように使う。表示したあとは
ディープスリープに入り、ボタンで復帰して選び直す。

[CardCase-For-M5Paper](https://github.com/mitsuharu/CardCase-For-M5Paper) の移植。
機能と操作は揃えているが、**M5Unified / M5GFX は使えない**ので描画まわりは作り直している。
設計の考え方（純粋ロジックを切り離す、機種差分を 1 か所に集める、コメントは日本語）は
そのまま引き継ぐ。

開発環境は VS Code + PlatformIO。

## 対象機種

| | H716 | H578 / H580 |
|---|---|---|
| SoC | ESP32-S3-WROOM-1-N16R8 | 同左 |
| Flash / PSRAM | 16MB / 8MB (OPI) | 同左 |
| 画面 | 4.7 inch ED047TC1 / 960×540 / 16 階調 | 同左 |
| タッチ | GT911（静電容量式） | **非搭載** |
| ボタン | RST / BOOT(IO0) / ユーザー(IO21) の 3 つ。使えるのは IO21 のみ | 同左 |
| microSD | SPI 配線 | 同左 |
| RTC | PCF8563 | 同左 |

PlatformIO の env は `T5-ePaper-S3` の 1 つだけ。タッチの有無は起動時に I2C を叩いて
判定するので、**同じバイナリが H716 でも H578 / H580 でも動く**。

機種ごとに次の点に注意する。

- **パネルは横長（960×540）で、回転の機能を持たない**。`epd_draw_grayscale_image()` は
  フレームバッファをそのまま転送するだけで、M5GFX の `setRotation()` にあたるものが無い。
  画面を回したいときは**書き込む座標の方を回す**。`Screen::putGray()` が回転を吸収するので、
  呼び出し側は論理座標だけを考えればよい
- **文字の描画（`writeln` / `write_mode`）は回転に対応していない**。ライブラリがフレーム
  バッファへ直接書くため。だから **UI は常に回転 0（横長）で描く**。回すのは画像だけにしている
- **基板のボタンは 3 つあるが、アプリが使えるのは IO21 の 1 つだけ**。

  | 基板 | ネット | GPIO | 役割 |
  | --- | --- | --- | --- |
  | S5 | CHIP_PU | – | RST（リセット） |
  | S6 | STR_IO0 | 0 | BOOT（ダウンロードモード） |
  | S4 | SENSOP_VN | 21 | ユーザーボタン。`Board::kButton` |

  **BOOT (IO0) を「ボタン」と呼ばないこと。** IO0 は e-paper の 74HCT4094（設定用
  シフトレジスタ）の STR ラッチと共用で、動作中に押すと表示が乱れる。画面に出す案内も
  `BTN(IO21)` と書いて取り違えを防ぐ。IO21 のネット名が `SENSOP_VN` なのは、ESP32 版で
  ボタンが SENSOR_VN (GPIO39) にあった名残（回路図 `T5-ePaper-S3-V2.3.pdf` で確認）。

- **使えるボタンが 1 つなので**「短押しで次へ、長押しで決定」で詰め込んである。
  タッチのある H716 でもボタン操作だけで完結できる状態を必ず保つこと
- **描画の前後で `epd_poweron()` / `epd_poweroff()` が要る**。付けっぱなしはパネルを痛める。
  `Screen::flush()` が対で呼ぶので、EPD の API を直接叩かないこと
- **遅さの正体は「白へ振る回数」**。`epd_clear_area()` は 4 サイクル、1 サイクルにつき
  黒 4 パス・白 4 パスなので 32 パス走る。そのあとの `epd_draw_grayscale_image()` は
  階調ぶんの 15 パス。つまり消す方が倍以上重い。部分更新を速くしたいときは、
  範囲を狭めるより先に `Screen::flushArea()` のサイクル数を見直す。
  所要時間は `CORE_DEBUG_LEVEL=4` 以上で `flushArea ... took N ms` として出る
- **フレームバッファは PSRAM に置く**（259,200 バイト）。内蔵 RAM には入らない
- **PNGdec は既定だと幅 320px までしか通らない**。`PNG_MAX_BUFFERED_PIXELS` の既定が
  `(320*4+1)*2 = 2562` バイトで、1 行がこれ以上になる PNG は `open()` の時点で
  `PNG_TOO_BIG`（7）を返す。パネルは 960px あるので、画面に合わせて送られてきた PNG が
  そのまま範囲外になる。`platformio.ini` で 16386（幅 2048px ぶん）に上げてある。
  この値は `PNGIMAGE` の中の固定配列の大きさなので、上げるとインスタンスがそのぶん太る
- **GT911 はタッチの状態をホストが読み取るまで保持する**。画面の全面更新は数秒かかり、
  その間こちらは I2C を読まない。更新が終わって最初に読むと「その画面を開くために押した
  タップ」がそのまま返ってきて、次の読み取りで離された扱いになり、開いた画面がその場で
  閉じる。時間で待っても保持された状態は消えないので防げない。`Input` は画面を切り替えた
  あと、**一度「触れていない」を読むまで新しい操作として扱わない**（`waitingForRelease`）。
  画面を切り替える処理を足すときは `Input::pauseTouch()` と `Input::discardPending()` を
  必ず対で呼ぶこと
- **スリープ復帰の直後はタッチが応答しない**。1 秒ほど待ってから I2C を叩く
  （[LilyGo-EPD47 の例](https://github.com/Xinyuan-LilyGO/LilyGo-EPD47/blob/master/examples/touch/touch.ino)
  にある通り）
- **タッチの割り込みピン（GPIO47）は RTC-IO ではない**ので、タッチでのスリープ復帰はできない。
  復帰は IO21 のボタンの ext1 wakeup だけ

## ディレクトリ構成

```
src/main.cpp     起動と画面の組み立てだけ。細かい処理はここに書かない
lib/
  Board/         ピン配置と画面レイアウトの定数
  Screen/        EPD のフレームバッファ。塗り・文字・回転つきのピクセル書き込み
  Storage/       microSD へのアクセス（SPI）
  ImageFile/     ファイル名の判定、EXIF と画像寸法の解析、表示する向きの決定
    （ExifOrientation / ImageSize / ImageRotation はすべて純粋ロジック）
  ImageDraw/     JPEG / PNG のデコードと、縮小・回転してフレームバッファへ載せる処理
    FitBox/      画面に収める矩形の計算（純粋ロジック）
  Input/         GT911 のタッチと BOOT ボタン
  Menu/          一覧の表示と選択。タッチとボタンの両対応
    Selection/   選択位置とページングの計算（純粋ロジック）
  WebTransfer/   WiFi（SoftAP）で画像を受け取る
    Credentials/ SSID とパスワードの組み立て（純粋ロジック）
test/            native 環境の単体テスト
```

## 設計ルール

### 純粋ロジックは `#ifdef ARDUINO` の外に置く

実機がなくても検証できる範囲を最大化するための、このリポジトリの中心的なルール。

- Arduino / EPD に依存しない計算・判定は、`#ifdef ARDUINO` の外に書いて native テストの対象にする
- 実機 API を叩く部分は `#ifdef ARDUINO` ... `#endif` で囲む
- ヘッダで `String` を使う場合は、native では `std::string` に読み替える

```cpp
#ifdef ARDUINO
#include <Arduino.h>
#else
#include <string>
using String = std::string;
#endif
```

`ImageFile.h` / `Selection.h` / `FitBox.h` / `Credentials.h` がこの形になっている。

### 画面の座標は「論理座標」で考える

`Screen` は回転（0〜3）を持つ。`Screen::width()` / `height()` は回転を反映した論理的な
大きさを返し、`putGray()` は論理座標をパネルの座標へ変換して書く。呼び出し側でパネルの
960×540 を直接扱わないこと。

ただし**文字はライブラリがフレームバッファへ直接書く**ので回転できない。文字を出す画面は
回転 0 のまま組む。

### コメントは日本語で書く

なぜそうしているかを書く。何をしているかはコードを読めば分かる。ハードウェア由来の理由
（パネルの制約、リフレッシュ時間、タッチの有無など）は必ず残す。

## ビルドと書き込み

PlatformIO CLI は `~/.platformio/penv/bin/pio` にある。PATH は通っていないので、
フルパスで叩くか、エイリアスを張ってから使うこと。

```bash
alias pio=~/.platformio/penv/bin/pio
```

```bash
# テスト（実機不要）
~/.platformio/penv/bin/pio test -e native

# ビルド
~/.platformio/penv/bin/pio run

# 書き込みとログ
~/.platformio/penv/bin/pio run -t upload
~/.platformio/penv/bin/pio device monitor
```

`default_envs` は `T5-ePaper-S3`。env の指定は省略できる。

`penv` が無い場合は入っていないので、公式のインストーラで作る。VS Code の
PlatformIO IDE 拡張も同じ場所を使う。

```bash
curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py -o get-platformio.py && python3 get-platformio.py
```

### 実機のログを見る

`ARDUINO_USB_CDC_ON_BOOT=1` を既定で入れてあるので、USB-C を挿すだけでシリアルが出る。
詳しく見たいときだけレベルを上げる。

```bash
PLATFORMIO_BUILD_FLAGS="-DCORE_DEBUG_LEVEL=5" ~/.platformio/penv/bin/pio run -t upload
```

`pio device monitor` は TTY を要求するので、対話端末以外からは使えない。その場合は
pyserial で直接読む。

### 書き込めないとき

**ディープスリープに入るとボードが USB から消える。** シリアルはチップ内蔵の
USB-JTAG/CDC で、スリープ中は列挙されなくなるため。画像を出したまま 60 秒放置すると
この状態になり、`/dev/cu.usbmodem*` がポート一覧から消える。

このとき PlatformIO は**別のポートを掴んで失敗する**。該当するポートが無いと
「VID:PID を持つ最後のポート」へ落ちる作りで、macOS では `/dev/cu.Bluetooth-Incoming-Port`
が選ばれる。`Failed to connect to ESP32-S3: No serial data received.` はほぼこれ。

復帰させてから書き込む。

1. RST を押す（または USB を挿し直す）。これで一覧に戻る
2. それでも繋がらないなら、**BOOT を押したまま RST を押して離す**。ROM の
   ダウンロードモードに入るので確実に書き込める。書き込み後は RST で再起動する

`boards/T5-ePaper-S3.json` の `hwids` は、LilyGo の配布版が `"0X303A"`（大文字の X）
になっていて自動判別が効かなかったため `"0x303A"` に直してある。PlatformIO は
`.replace("0x", "")` で正規化するので、大文字だと `0X303A:1001` のまま残って
実機の `303A:1001` と一致しない。上流を取り込み直すときは戻さないこと。

## テスト

- **CI で回すのは native テストのみ**。`Arduino.h` を必要とするコードは CI でビルドはできてもテストはできない
- CI では `pip install platformio` で入れているので PATH が通る。手元とはコマンドの書き方が変わる
- テストは `test/<スイート名>/` に置く。各ファイルが `main()` を持つため、`test/` 直下に複数のファイルを並べるとリンクに失敗する

実機でしか確認できないこと（描画結果、タッチ、スリープ復帰、SD の読み書き、WiFi の受信）は
手動で確認する。

## 実機で未確認の箇所

移植の初版は実機なしで書いている。次の点は実機で最初に確かめること。

1. **microSD がマウントできるか**（`Storage`）。SPI の 4 本は `utilities.h` の値をそのまま使っている
2. **画像の階調**（`ImageDraw`）。4×4 の組織的ディザを入れてあるが、強すぎ / 弱すぎは実機で見ないと分からない
   （デコードの失敗は `log_e` で出るので、`CORE_DEBUG_LEVEL=1` のままでもシリアルに理由が出る）
3. **画像を回す向き**（`ImageDraw` の `kFitSteps`）。時計回りにしてある。逆に感じたら 1 行変える
4. **ゴーストの残り方**（`Screen::flush(true)`）。QR のあとは全面クリアを挟んでいる

### 実機で確認できたこと

- 一覧の表示とタッチによる選択は動く（H716、2026-09-06）
- 座標変換（`setSwapXY(true)` / `setMirrorXY(false, true)`）は LilyGo の例のままで合っている
- GT911 の保持されたタッチで、開いた画面がその場で閉じる問題があった。上の
  「対象機種」の注意書きにある `waitingForRelease` で対処済み
- QR の表示とスマホからの送信、受け取った画像の表示まで動く。PNG が表示できなかったのは
  PNGdec の行バッファの既定値が原因で、`PNG_MAX_BUFFERED_PIXELS` を上げて対処済み
- IO21 のボタンでの一覧操作は動く。カーソル移動の部分更新は、動く 2 行をまとめて
  白振りを 2 サイクルへ落としたことで体感できるほど速くなった（2026-09-06）

## リリース

未整備。CardCase-For-M5Paper と同じく GitHub Actions で統合バイナリを作る予定。
