#pragma once

/**
 * 基板まわりの定数。
 *
 * ピン番号は LilyGo-EPD47 の utilities.h が持っているので、そちらを唯一の出どころにして
 * ここでは名前を付け直すだけにする。番号を書き写すと、ライブラリが更新されたときに
 * こちらだけ古いまま残る。
 */

#ifdef ARDUINO

#include <utilities.h>

namespace Board
{
    // microSD（SPI 配線）
    constexpr int kSdMiso = SD_MISO; // 16
    constexpr int kSdMosi = SD_MOSI; // 15
    constexpr int kSdSclk = SD_SCLK; // 11
    constexpr int kSdCs = SD_CS;     // 42

    // タッチ（GT911）。H716 のみ搭載で、H578 / H580 には無い。
    constexpr int kI2cSda = BOARD_SDA;   // 18
    constexpr int kI2cScl = BOARD_SCL;   // 17
    constexpr int kTouchInt = TOUCH_INT; // 47

    /**
     * 自由に使えるボタン（基板の S4、押すと Low）。
     *
     * 基板にはボタンが 3 つあるが、使えるのはこれだけ。
     *   - RST (S5) … CHIP_PU。リセット
     *   - BOOT (S6) … IO0。ダウンロードモード用。e-paper の 74HCT4094 の
     *     STR ラッチと共用なので、動作中に押すと表示が乱れる
     *   - S4 … IO21。これ
     *
     * 回路図のネット名は SENSOP_VN だが、ESP32 版でボタンが SENSOR_VN (GPIO39)
     * にあった名残で、S3 版では IO21 に繋がっている。
     */
    constexpr int kButton = BUTTON_1; // 21

    // 電池電圧の分圧。読むときは 2 倍する。
    constexpr int kBatteryAdc = BATT_PIN; // 14
}

/**
 * 画面のレイアウト。
 *
 * パネルは 960×540 の横長で回転を持たないため、UI はこの向きに固定して組む
 * （文字の描画がフレームバッファへ直接書く作りで、回すと読めなくなるため）。
 * 数値はすべて回転 0 の座標。
 */
namespace Layout
{
    constexpr int kPanelWidth = 960;
    constexpr int kPanelHeight = 540;

    /// 縁は筐体に隠れることがあるので、端まで描かない
    constexpr int kMargin = 20;

    /// 見出しのベースライン（FiraSans の ascender ぶん下げる）
    constexpr int kHeaderBaseline = 59;

    /// 見出しと一覧を分ける罫線
    constexpr int kRuleY = 78;

    /// 一覧の上端と 1 行の高さ（Roboto18 の advance_y は 44）
    constexpr int kListTop = 90;
    constexpr int kRowHeight = 46;
    constexpr int kVisibleRows = 8;

    /// フッタ（ページ表示・操作ガイド・ページ送りボタン）
    constexpr int kFooterTop = 462;
    constexpr int kFooterLine1Baseline = 490;
    constexpr int kFooterLine2Baseline = 519;

    constexpr int kPagerWidth = 104;
    constexpr int kPagerHeight = 48;
    constexpr int kPagerTop = 466;
    constexpr int kPagerNextX = kPanelWidth - kMargin - kPagerWidth;
    constexpr int kPagerPrevX = kPagerNextX - 12 - kPagerWidth;
}

#endif
