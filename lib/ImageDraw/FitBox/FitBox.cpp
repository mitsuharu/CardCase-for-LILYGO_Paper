#include "FitBox.h"

namespace
{
    long long ceilDiv(long long numerator, long long denominator)
    {
        if (denominator <= 0)
        {
            return 0;
        }
        return (numerator + denominator - 1) / denominator;
    }

    int clampInt(long long value, int low, int high)
    {
        if (value < low)
        {
            return low;
        }
        if (value > high)
        {
            return high;
        }
        return static_cast<int>(value);
    }
}

namespace ImageDraw
{
    FitBox fitInto(int srcWidth, int srcHeight, int destWidth, int destHeight)
    {
        FitBox box;
        if (srcWidth <= 0 || srcHeight <= 0 || destWidth <= 0 || destHeight <= 0)
        {
            return box;
        }

        long long sw = srcWidth;
        long long sh = srcHeight;
        long long dw = destWidth;
        long long dh = destHeight;

        long long width = 0;
        long long height = 0;

        // sw/sh と dw/dh を掛け算で比べる。割り算にすると丸めで判定がぶれる。
        if (sw * dh <= dw * sh)
        {
            // 縦が先に埋まる
            height = dh;
            width = (sw * dh + sh / 2) / sh;
        }
        else
        {
            width = dw;
            height = (sh * dw + sw / 2) / sw;
        }

        box.width = clampInt(width, 1, destWidth);
        box.height = clampInt(height, 1, destHeight);
        box.x = (destWidth - box.width) / 2;
        box.y = (destHeight - box.height) / 2;
        return box;
    }

    int decodeScaleDivisor(int srcWidth, int srcHeight, int fitWidth, int fitHeight)
    {
        if (srcWidth <= 0 || srcHeight <= 0 || fitWidth <= 0 || fitHeight <= 0)
        {
            return 1;
        }

        const int candidates[] = {8, 4, 2};
        for (int divisor : candidates)
        {
            if (srcWidth / divisor >= fitWidth && srcHeight / divisor >= fitHeight)
            {
                return divisor;
            }
        }
        return 1;
    }

    Span destSpan(int srcBegin, int srcCount, int srcTotal, int destOrigin, int destSize)
    {
        Span span;
        span.begin = destOrigin;
        span.end = destOrigin;

        if (srcCount <= 0 || srcTotal <= 0 || destSize <= 0)
        {
            return span;
        }

        long long first = srcBegin;
        long long last = static_cast<long long>(srcBegin) + srcCount;

        // 描画先 d が参照する元画像の位置は floor((d - destOrigin) * srcTotal / destSize)。
        // それが [first, last) に入る d の範囲を逆に解く。
        long long begin = ceilDiv(first * destSize, srcTotal);
        long long end = ceilDiv(last * destSize, srcTotal);

        span.begin = destOrigin + clampInt(begin, 0, destSize);
        span.end = destOrigin + clampInt(end, 0, destSize);
        if (span.end < span.begin)
        {
            span.end = span.begin;
        }
        return span;
    }
}
