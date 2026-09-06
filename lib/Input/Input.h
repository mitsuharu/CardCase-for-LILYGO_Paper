#pragma once

#ifdef ARDUINO

#include <Arduino.h>

/**
 * タッチとボタンの入力。
 *
 * タッチ（GT911）は H716 にしか無い。H578 / H580 では起動時の I2C 走査で見つからず、
 * ボタンだけの操作になる。同じバイナリで両方に対応するため、機種の判定は
 * ビルド時ではなく実行時に行う。
 *
 * ボタンは BOOT の 1 つだけなので、短押しと長押しに 2 つの役割を割り当てている。
 */
namespace Input
{
    struct Point
    {
        int x = 0;
        int y = 0;
    };

    /// 長押しと見なす時間
    constexpr unsigned long kLongPressMs = 700;

    /**
     * 初期化する。タッチが見つかれば true を返す。
     * 見つからなくてもボタンは使えるので、呼び出し側は止まらないこと。
     */
    bool begin();

    /// タッチを搭載しているか
    bool hasTouch();

    /// loop() から呼ぶ
    void update();

    /**
     * タッチが離れた位置。押した瞬間ではなく離した瞬間に 1 回だけ true を返す。
     *
     * 電子ペーパーは 1 回の更新に数百 ms かかり、その間は処理が止まって
     * 押しっぱなしの状態を取りこぼす。押した瞬間で拾うと、更新の間に指が
     * 触れ続けているだけで次の操作として誤検出される。
     */
    bool wasTapped(Point &at);

    /// ボタンの短押し
    bool wasClicked();

    /// ボタンの長押し
    bool wasLongPressed();

    /**
     * 何らかの操作があったか。タッチでもボタンでもよい場面で使う。
     */
    bool wasAnyInput();

    /**
     * ボタンの操作があったか（短押しか長押し）。タッチは見ない。
     *
     * 画像を表示している間はこちらを使う。相手に画面を見せている最中の
     * 誤タップで画像が消えてしまうため。
     */
    bool wasButtonInput();

    /// 溜まっているタッチだけを捨てる。ボタンの操作は残す。
    void discardTouch();

    /// 拾っていない入力を捨てる。画面を切り替えた直後の誤爆を防ぐ。
    void discardPending();

    /**
     * 画面の更新の直後は、しばらくタッチを読まない。
     *
     * 転送中は I2C を触れず、直後もタッチ側が落ち着いていないため。
     * Screen::flush() を呼んだあとに続けて呼ぶ。
     */
    void pauseTouch(unsigned long durationMs = 300);

    /// ディープスリープの前に呼ぶ。タッチを寝かせて I2C を解放する。
    void end();
}

#endif
