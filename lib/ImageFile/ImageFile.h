#pragma once

/**
 * SD カード上のファイル名の判定。
 * Arduino に依存しない純粋関数だけを置き、native 環境で単体テストする。
 */

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <string>
using String = std::string;
#endif

namespace ImageFile
{
    /**
     * 対応する画像ファイルかどうか。
     * 拡張子は大文字小文字を区別しない（デジカメの IMG_0001.JPG などを取りこぼさないため）。
     */
    bool isSupportedImage(const String &name);

    /// 隠しファイルかどうか。macOS が SD に作る "._foo.jpg" や ".DS_Store" を除外する。
    bool isHidden(const String &name);

    /// 一覧に載せる対象か（隠しファイルではない画像）
    bool isListable(const String &name);

    /// ルート直下のファイル名を絶対パスにする
    String rootPath(const String &name);
}
