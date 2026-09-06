#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <string>
using String = std::string;
#endif

#include <stdint.h>

/**
 * 画像を受け取るときのアクセスポイントの接続情報。
 *
 * 文字列を組み立てるだけの純粋なロジックなので native 環境で単体テストする。
 */
namespace Credentials
{
    /// WPA2 のパスワードの長さ（8 文字以上でないと接続できない）
    const int kPasswordLength = 8;

    /**
     * MAC アドレスから SSID を作る。
     * 同じ本体なら毎回同じになるので、スマホがネットワークを覚えていられる。
     */
    String ssidFor(const uint8_t *mac);

    /**
     * MAC アドレスからパスワードを作る。
     *
     * 起動のたびに変えるとスマホ側が覚えた古いパスワードで接続に失敗し、
     * ネットワーク設定の削除が要る。毎回同じにして繋ぎ直しやすさを取る。
     * 画面に QR とあわせて表示するので、秘匿を目的にはしていない。
     */
    String passwordFor(const uint8_t *mac);

    /**
     * カメラで読ませる WiFi 接続用の文字列を作る。
     *
     * 「WIFI:T:WPA;S:<SSID>;P:<パスワード>;;」の形式。iPhone のカメラでも
     * Android でも、読み取るだけでアクセスポイントに繋がる。
     * SSID とパスワードに含まれる特殊文字はエスケープする。
     */
    String wifiQrPayload(const String &ssid, const String &password);
}
