#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#include <FS.h>
#endif

/**
 * microSD へのアクセス。
 *
 * T5 4.7 の SD は SPI 配線で、EPD（I2S パラレル）ともタッチ（I2C）とも
 * バスを共有していない。描画の途中で読んでも競合しない。
 */
namespace Storage
{
#ifdef ARDUINO
    /**
     * マウントする。復帰直後などは一度で成功しないことがあるのでやり直す。
     */
    bool begin();

    /**
     * アンマウントする。
     *
     * ディープスリープ中も SD カードには通電したままなので、読み出しの途中で寝ると
     * カードが中途半端な状態で残り、次回のマウントが失敗する。寝る前に必ず呼ぶ。
     */
    void end();

    /// マウントできているか。SD が無くても動く処理から使う。
    bool isAvailable();

    /// 画像を読むためのファイルシステム。begin() が成功したあとに使う。
    fs::FS &fs();
#endif
}
