#include "Input.h"

#ifdef ARDUINO

#include <Wire.h>
#include <Board.h>
#include <TouchDrvGT911.hpp>

namespace
{
    // GT911 は RST がハードウェアでプルアップされていてアドレスを指定できないので、
    // 取りうる 2 つを走査して見つける（LilyGo のサンプルと同じ方法）。
    constexpr uint8_t kGt911Addresses[] = {0x14, 0x5D};

    TouchDrvGT911 touch;
    bool touchAvailable = false;

    bool touching = false;
    bool tapped = false;
    Input::Point tapPoint;
    unsigned long touchPausedUntil = 0;

    // ボタンは Button2 を使わず自前で見る。
    // 短押しと長押しの 2 つしか要らず、状態も押下時刻だけで足りるため。
    bool buttonDown = false;
    unsigned long buttonDownAt = 0;
    bool clicked = false;
    bool longPressed = false;

    // チャタリング除け。機械式の接点なので数 ms 単位で暴れる。
    constexpr unsigned long kDebounceMs = 30;
    unsigned long buttonChangedAt = 0;

    void updateButton()
    {
        bool down = (digitalRead(Board::kButton) == LOW);
        unsigned long now = millis();

        if (down == buttonDown)
        {
            return;
        }
        if (now - buttonChangedAt < kDebounceMs)
        {
            return;
        }
        buttonChangedAt = now;
        buttonDown = down;

        if (down)
        {
            buttonDownAt = now;
            return;
        }

        // 離した時点で長さを見て振り分ける。
        // 押している最中に判定すると、長押しのつもりで押し続けている間に
        // 決定が走り、指を離したときにもう一度短押しが混ざる。
        if (now - buttonDownAt >= Input::kLongPressMs)
        {
            longPressed = true;
        }
        else
        {
            clicked = true;
        }
    }

    void updateTouch()
    {
        if (!touchAvailable)
        {
            return;
        }
        if (static_cast<long>(millis() - touchPausedUntil) < 0)
        {
            return;
        }

        int16_t x = 0;
        int16_t y = 0;
        bool down = (touch.getPoint(&x, &y, 1) > 0);

        if (down)
        {
            // 離した位置を使いたいので、触れている間ずっと最後の座標を覚えておく
            tapPoint.x = x;
            tapPoint.y = y;
            touching = true;
            return;
        }

        if (touching)
        {
            touching = false;
            tapped = true;
        }
    }
}

namespace Input
{
    bool begin()
    {
        pinMode(Board::kButton, INPUT_PULLUP);

        // スリープから復帰した直後はタッチが応答しないので、少し待ってから叩く
        if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED)
        {
            delay(1000);
        }

        Wire.begin(Board::kI2cSda, Board::kI2cScl);

        // 直前にタッチを寝かせている可能性があるので、割り込み線を上げて起こす
        pinMode(Board::kTouchInt, OUTPUT);
        digitalWrite(Board::kTouchInt, HIGH);

        uint8_t address = 0;
        for (uint8_t candidate : kGt911Addresses)
        {
            Wire.beginTransmission(candidate);
            if (Wire.endTransmission() == 0)
            {
                address = candidate;
                break;
            }
        }

        if (address == 0)
        {
            // タッチ非搭載の版（H578 / H580）。ボタンだけで動かす。
            log_i("touch is not available");
            touchAvailable = false;
            return false;
        }

        touch.setPins(-1, Board::kTouchInt);
        if (!touch.begin(Wire, address, Board::kI2cSda, Board::kI2cScl))
        {
            log_w("failed to start GT911 at 0x%02X", address);
            touchAvailable = false;
            return false;
        }

        // パネルの向きに合わせる。基板の版で変わることがあるので、
        // 実機で座標がずれたらここだけ直す。
        touch.setMaxCoordinates(Layout::kPanelWidth, Layout::kPanelHeight);
        touch.setSwapXY(true);
        touch.setMirrorXY(false, true);

        touchAvailable = true;

        // 起動直後は読んでも応答しない
        pauseTouch(300);
        return true;
    }

    bool hasTouch()
    {
        return touchAvailable;
    }

    void update()
    {
        updateButton();
        updateTouch();
    }

    bool wasTapped(Point &at)
    {
        if (!tapped)
        {
            return false;
        }
        tapped = false;
        at = tapPoint;
        return true;
    }

    bool wasClicked()
    {
        if (!clicked)
        {
            return false;
        }
        clicked = false;
        return true;
    }

    bool wasLongPressed()
    {
        if (!longPressed)
        {
            return false;
        }
        longPressed = false;
        return true;
    }

    bool wasAnyInput()
    {
        Point ignored;
        bool touchInput = wasTapped(ignored);
        bool clickInput = wasClicked();
        bool longInput = wasLongPressed();
        return touchInput || clickInput || longInput;
    }

    void discardPending()
    {
        tapped = false;
        clicked = false;
        longPressed = false;
    }

    void pauseTouch(unsigned long durationMs)
    {
        touchPausedUntil = millis() + durationMs;

        // 更新の間に触れていた場合、離した扱いにすると誤って選ばれる
        touching = false;
        tapped = false;
    }

    void end()
    {
        if (touchAvailable)
        {
            touch.sleep();
            delay(5);
        }

        Wire.end();

        // 入力のままにするとスリープ中に電流が流れ続ける
        pinMode(Board::kI2cSda, OPEN_DRAIN);
        pinMode(Board::kI2cScl, OPEN_DRAIN);
        pinMode(Board::kTouchInt, OPEN_DRAIN);
    }
}

#endif
