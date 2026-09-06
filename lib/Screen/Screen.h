#pragma once

#ifdef ARDUINO

#include <Arduino.h>
#include <epd_driver.h>

/**
 * 電子ペーパー（ED047TC1）への描画。
 *
 * パネルは 960×540 の横長で、M5GFX の setRotation() にあたる機能を持たない。
 * epd_draw_grayscale_image() はフレームバッファをそのまま転送するだけなので、
 * 画面を回したいときは「書き込む座標の方を回す」。その変換をここへ閉じ込める。
 *
 * ただし文字の描画はライブラリがフレームバッファへ直接書くため回せない。
 * 文字を含む画面は回転 0 のまま組むこと（drawText は回転を無視する）。
 *
 * 色は 8bit のグレー（0 = 黒、255 = 白）で受け取る。フレームバッファ上では
 * 上位 4bit だけが使われる。
 */
namespace Screen
{
    constexpr uint8_t kBlack = 0x00;
    constexpr uint8_t kWhite = 0xFF;

    /// 4bit の色（0 = 黒、15 = 白）。文字の前景・背景色に使う。
    constexpr uint8_t kInk = 0;
    constexpr uint8_t kPaper = 15;

    /**
     * 初期化する。フレームバッファ（259,200 バイト）は PSRAM に取る。
     * 内蔵 RAM には入らないので、確保できなければ false を返す。
     */
    bool begin();

    /// 回転（0〜3、時計回りに 90 度ずつ）。画像を出すときだけ 0 以外にする。
    void setRotation(int rotation);
    int rotation();

    /// 回転を反映した論理的な画面の大きさ
    int width();
    int height();

    uint8_t *framebuffer();

    /// フレームバッファを 1 色で塗る（転送はしない）
    void clear(uint8_t gray = kWhite);

    /// 論理座標に点を打つ。回転はここで吸収する。
    void putGray(int x, int y, uint8_t gray);

    /// 論理座標の矩形を塗る
    void fillRect(int x, int y, int w, int h, uint8_t gray);

    /// 論理座標の矩形を線で描く
    void drawRect(int x, int y, int w, int h, uint8_t gray);

    /// 論理座標の横線
    void drawHLine(int x, int y, int length, uint8_t gray);

    /**
     * 文字を描く。y はベースラインの位置。
     *
     * 回転には対応していない（ライブラリの制約）。回転 0 のときだけ使うこと。
     * 反転表示にしたいときは、先に fillRect で下地を塗ってから fg / bg を入れ替える。
     */
    void drawText(const GFXfont *font, const char *text, int x, int baselineY,
                  uint8_t fg = kInk, uint8_t bg = kPaper);

    /// 文字列の幅（ピクセル）
    int textWidth(const GFXfont *font, const char *text);

    /**
     * 幅に収まるように末尾を "..." に詰めた文字列を返す。
     * ファイル名は長くなりがちで、そのまま描くと画面からはみ出す。
     */
    String ellipsize(const GFXfont *font, const String &text, int maxWidth);

    /**
     * フレームバッファをパネルへ転送する。
     *
     * full を true にすると転送の前に全面を白黒で振り、前の絵を消し切る。
     * QR コードのような細かい模様のあとは、これをしないと次の絵に透ける。
     */
    void flush(bool full = false);

    /// パネルの電源を落とす。スリープや電源断の前に必ず呼ぶ。
    void powerOff();

    /// 画面で使うフォント。実体を 1 か所に集めるため関数で配る。
    namespace Fonts
    {
        /// 見出し用（advance_y 50）
        const GFXfont *header();

        /// 一覧の 1 行用（advance_y 44）
        const GFXfont *row();

        /// フッタや注記用（advance_y 29）
        const GFXfont *small();
    }
}

#endif
