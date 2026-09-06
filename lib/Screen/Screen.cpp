#include "Screen.h"

#ifdef ARDUINO

#include <Board.h>

// フォントの実体はヘッダに入っている（const なので翻訳単位ごとに複製される）。
// Flash を無駄にしないよう、取り込むのはこのファイルだけにして関数で配る。
#include <firasans.h>
#include <roboto12.h>
#include <roboto18.h>

namespace
{
    uint8_t *buffer = nullptr;
    int rotationSteps = 0;

    /// 論理座標をパネルの座標へ移す
    inline void toPanel(int x, int y, int &px, int &py)
    {
        switch (rotationSteps)
        {
        case 1: // 時計回りに 90 度
            px = Layout::kPanelWidth - 1 - y;
            py = x;
            break;
        case 2:
            px = Layout::kPanelWidth - 1 - x;
            py = Layout::kPanelHeight - 1 - y;
            break;
        case 3:
            px = y;
            py = Layout::kPanelHeight - 1 - x;
            break;
        default:
            px = x;
            py = y;
            break;
        }
    }
}

namespace Screen
{
    bool begin()
    {
        epd_init();

        if (buffer == nullptr)
        {
            buffer = static_cast<uint8_t *>(ps_calloc(sizeof(uint8_t), EPD_WIDTH * EPD_HEIGHT / 2));
        }
        if (buffer == nullptr)
        {
            return false;
        }

        clear();
        return true;
    }

    void setRotation(int rotation)
    {
        rotationSteps = rotation & 3;
    }

    int rotation()
    {
        return rotationSteps;
    }

    int width()
    {
        return (rotationSteps & 1) ? Layout::kPanelHeight : Layout::kPanelWidth;
    }

    int height()
    {
        return (rotationSteps & 1) ? Layout::kPanelWidth : Layout::kPanelHeight;
    }

    uint8_t *framebuffer()
    {
        return buffer;
    }

    void clear(uint8_t gray)
    {
        if (buffer == nullptr)
        {
            return;
        }
        // 4bit を 2 画素ぶん詰めるので、上位と下位に同じ値を置く
        uint8_t packed = static_cast<uint8_t>((gray & 0xF0) | (gray >> 4));
        memset(buffer, packed, EPD_WIDTH * EPD_HEIGHT / 2);
    }

    void putGray(int x, int y, uint8_t gray)
    {
        if (buffer == nullptr || x < 0 || y < 0 || x >= width() || y >= height())
        {
            return;
        }

        int px = 0;
        int py = 0;
        toPanel(x, y, px, py);
        epd_draw_pixel(px, py, gray, buffer);
    }

    void fillRect(int x, int y, int w, int h, uint8_t gray)
    {
        if (buffer == nullptr || w <= 0 || h <= 0)
        {
            return;
        }

        // 90 度単位の回転では矩形は矩形のままなので、対角の 2 点だけ移せばよい
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        toPanel(x, y, x0, y0);
        toPanel(x + w - 1, y + h - 1, x1, y1);

        int left = (x0 < x1) ? x0 : x1;
        int top = (y0 < y1) ? y0 : y1;
        int right = (x0 > x1) ? x0 : x1;
        int bottom = (y0 > y1) ? y0 : y1;

        epd_fill_rect(left, top, right - left + 1, bottom - top + 1, gray, buffer);
    }

    void drawRect(int x, int y, int w, int h, uint8_t gray)
    {
        if (w <= 0 || h <= 0)
        {
            return;
        }
        drawHLine(x, y, w, gray);
        drawHLine(x, y + h - 1, w, gray);
        for (int i = 0; i < h; i++)
        {
            putGray(x, y + i, gray);
            putGray(x + w - 1, y + i, gray);
        }
    }

    void drawHLine(int x, int y, int length, uint8_t gray)
    {
        for (int i = 0; i < length; i++)
        {
            putGray(x + i, y, gray);
        }
    }

    void drawText(const GFXfont *font, const char *text, int x, int baselineY,
                  uint8_t fg, uint8_t bg)
    {
        if (buffer == nullptr || font == nullptr || text == nullptr || text[0] == '\0')
        {
            return;
        }

        FontProperties props = {};
        props.fg_color = fg & 0x0F;
        props.bg_color = bg & 0x0F;
        props.fallback_glyph = '?';
        props.flags = 0;

        int32_t cursorX = x;
        int32_t cursorY = baselineY;
        write_mode(font, text, &cursorX, &cursorY, buffer, BLACK_ON_WHITE, &props);
    }

    int textWidth(const GFXfont *font, const char *text)
    {
        if (font == nullptr || text == nullptr || text[0] == '\0')
        {
            return 0;
        }

        int32_t x = 0, y = 0, x1 = 0, y1 = 0, w = 0, h = 0;
        get_text_bounds(font, text, &x, &y, &x1, &y1, &w, &h, nullptr);
        return static_cast<int>(w);
    }

    String ellipsize(const GFXfont *font, const String &text, int maxWidth)
    {
        if (textWidth(font, text.c_str()) <= maxWidth)
        {
            return text;
        }

        // 1 文字ずつ削って収まるところを探す。
        // 文字ごとに幅が違うので、比率からの逆算では収まらないことがある。
        String truncated = text;
        while (truncated.length() > 1)
        {
            truncated.remove(truncated.length() - 1);
            String candidate = truncated + "...";
            if (textWidth(font, candidate.c_str()) <= maxWidth)
            {
                return candidate;
            }
        }
        return String("...");
    }

    void flush(bool full)
    {
        if (buffer == nullptr)
        {
            return;
        }

        epd_poweron();
        if (full)
        {
            epd_clear();
        }
        epd_draw_grayscale_image(epd_full_screen(), buffer);
        epd_poweroff();
    }

    void flushArea(int x, int y, int w, int h)
    {
        if (buffer == nullptr || w <= 0 || h <= 0)
        {
            return;
        }

        int left = x & ~1;
        int right = (x + w + 1) & ~1;
        if (left < 0)
        {
            left = 0;
        }
        if (right > EPD_WIDTH)
        {
            right = EPD_WIDTH;
        }

        int top = (y < 0) ? 0 : y;
        int bottom = (y + h > EPD_HEIGHT) ? EPD_HEIGHT : y + h;

        int areaWidth = right - left;
        int areaHeight = bottom - top;
        if (areaWidth <= 0 || areaHeight <= 0)
        {
            return;
        }

        // 転送には範囲ぶんの連続した領域が要る。フレームバッファは画面幅で
        // 並んでいるので、行ごとに切り出して詰め直す。
        size_t stride = static_cast<size_t>(areaWidth) / 2;
        uint8_t *slice = static_cast<uint8_t *>(ps_malloc(stride * areaHeight));
        if (slice == nullptr)
        {
            // 取れないなら全面で出す。遅いが表示は正しい。
            flush();
            return;
        }

        for (int row = 0; row < areaHeight; row++)
        {
            memcpy(slice + stride * row,
                   buffer + static_cast<size_t>(top + row) * (EPD_WIDTH / 2) + left / 2,
                   stride);
        }

        Rect_t area = {left, top, areaWidth, areaHeight};

        epd_poweron();
        // 描き足すだけでは前の絵が残る（白へ戻す操作にならない）ので、
        // 先にこの範囲を白へ振る
        epd_clear_area(area);
        epd_draw_grayscale_image(area, slice);
        epd_poweroff();

        free(slice);
    }

    void powerOff()
    {
        epd_poweroff_all();
    }

    namespace Fonts
    {
        const GFXfont *header()
        {
            return &FiraSans;
        }

        const GFXfont *row()
        {
            return &Roboto18;
        }

        const GFXfont *small()
        {
            return &Roboto12;
        }
    }
}

#endif
