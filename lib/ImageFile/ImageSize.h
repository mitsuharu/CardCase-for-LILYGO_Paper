#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * 画像ファイルからピクセル寸法を読む。
 *
 * バイト列を読むだけの純粋なロジックなので native 環境で単体テストする。
 */
namespace ImageFile
{
    /**
     * JPEG または PNG の先頭バイト列からピクセル寸法を読む。
     * 読めた場合だけ width / height に書き込んで true を返す。
     *
     * JPEG は APPn セグメントを読み飛ばして SOF から取る。EXIF にサムネイルが
     * 入っていると SOF は先頭から数十 KB 先になるので、data には十分な長さが要る。
     */
    bool imageSize(const uint8_t *data, size_t size, int *width, int *height);

    /**
     * 中身から拡張子を決める。
     *
     * 受け取った画像を保存するときに使う。送られてくる形式は JPEG と PNG の
     * どちらもありうるが、名前は付いてこないので中身から判断する。
     * どちらでもない場合は nullptr。
     */
    const char *extensionFor(const uint8_t *data, size_t size);
}
