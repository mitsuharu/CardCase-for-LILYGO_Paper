#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#endif

#include "Credentials/Credentials.h"

/**
 * スマホから画像を受け取る。
 *
 * 本体がアクセスポイントを立て、画面に接続用の QR コードを出す。
 * スマホで読み取って繋ぐとブラウザが自動でアップロード画面を開くので、
 * 画像を選ぶだけで SD に保存される。アプリの用意は要らない。
 *
 * 縮小と向きの補正はブラウザ側で行い、本体は保存するだけにしている。
 * 減色はパネルへ描くときに行うので、送る側では何もしない。
 */
namespace WebTransfer
{
#ifdef ARDUINO
    /// アクセスポイントを立ててサーバを開始する
    bool begin();

    /// loop() から呼ぶ
    void update();

    /// 停止して電波を止める
    void end();

    /// 画像を受け取ったか
    bool hasReceivedImage();

    /**
     * 受け取った画像のデータ。
     * SD が無くても表示できるよう、いったんメモリに持っている。
     */
    const uint8_t *receivedImage(size_t &size);

    /// SD に保存できた場合のパス。保存していなければ空。
    String receivedImagePath();

    /// 受け取った画像を解放する
    void releaseReceivedImage();

    /**
     * 接続を待っている画面をフレームバッファへ描く。
     * パネルへの転送は呼び出し側がまとめて行う。
     */
    void render();
#endif
}
