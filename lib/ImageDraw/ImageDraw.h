#pragma once

#ifdef ARDUINO

#include <Arduino.h>

/**
 * 画像をフレームバッファへ載せる。
 *
 * M5GFX の drawJpg / drawPng にあたるものが無いので、デコード・縮小・回転・
 * 減色を自前で行う。パネルへの転送は呼び出し側（Screen::flush）に任せる。
 * 電子ペーパーの更新は数百 ms かかるので、まとめて 1 回にしたいため。
 *
 * 表示する向きはここで決めて Screen::setRotation() を呼ぶ。判断の材料は
 * EXIF の向きと、画像が縦長か横長かの 2 つ（ImageFile::displayRotation）。
 */
namespace ImageDraw
{
    /// 画面に収まらない画像を送られたときのために、扱える大きさの上限を決めておく
    constexpr int kMaxSourceWidth = 8192;
    constexpr int kMaxSourceHeight = 8192;

    /// SD 上の画像を描く
    bool drawFile(const String &path);

    /// メモリ上の画像を描く。SD が無くても WiFi で受けた画像を出せるようにする。
    bool drawMemory(const uint8_t *data, size_t size);
}

#endif
