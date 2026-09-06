#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * JPEG の EXIF から Orientation を読む。
 *
 * スマホやデジカメで撮った写真は、縦向きに撮ってもピクセルとしては横長のまま保存され、
 * 「表示するときに回せ」という向きの情報だけが EXIF に入っていることが多い。
 * JPEG のデコーダはこの情報を見ないので、そのまま描くと横倒しになる。
 *
 * バイト列を読むだけの純粋なロジックなので native 環境で単体テストする。
 */
namespace ImageFile
{
    /// Orientation が読めなかったときの値（回転なし）
    const int kDefaultOrientation = 1;

    /**
     * JPEG の先頭バイト列から EXIF Orientation（1〜8）を返す。
     * EXIF が無い場合や壊れている場合は kDefaultOrientation を返す。
     *
     * data はファイルの先頭から連続した領域であればよく、全体でなくてよい。
     */
    int exifOrientation(const uint8_t *data, size_t size);

    /**
     * Orientation を「時計回りに 90 度を何回」に変換する（0〜3）。
     * 画面の回転（Screen::setRotation）に足して使う。
     *
     * 鏡像を伴う向き（2,4,5,7）は回転だけで近似する。
     * 実機では鏡像に対応できず、写真でも滅多に使われないため。
     */
    int rotationStepsFor(int orientation);
}
