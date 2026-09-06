#include "ImageOrder.h"

namespace
{
    char toLowerAscii(char c)
    {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }

    bool isDigit(char c)
    {
        return c >= '0' && c <= '9';
    }

    /**
     * 位置 i から続く数字の並びを読む。
     *
     * 値そのものではなく「先頭の 0 を除いた並び」で比べる。桁数が実際の
     * 桁数を超えることがあり（`0000000000000001.jpg` のような名前）、
     * 数値にすると溢れるため。
     */
    void scanDigits(const String &value, size_t &i, size_t &start, size_t &length)
    {
        while (i < value.length() && value[i] == '0')
        {
            i++;
        }
        start = i;
        while (i < value.length() && isDigit(value[i]))
        {
            i++;
        }
        length = i - start;
    }

    /// -1: a が前、1: b が前、0: 同じ
    int compareDigitRuns(const String &a, size_t aStart, size_t aLength,
                         const String &b, size_t bStart, size_t bLength)
    {
        // 桁数が違えば、多い方が大きい（先頭の 0 は落としてある）
        if (aLength != bLength)
        {
            return (aLength < bLength) ? -1 : 1;
        }
        for (size_t k = 0; k < aLength; k++)
        {
            char ca = a[aStart + k];
            char cb = b[bStart + k];
            if (ca != cb)
            {
                return (ca < cb) ? -1 : 1;
            }
        }
        return 0;
    }

    /// 文字コードでの比較。順番を確定させる最後の手段。
    int compareRaw(const String &a, const String &b)
    {
        size_t limit = (a.length() < b.length()) ? a.length() : b.length();
        for (size_t i = 0; i < limit; i++)
        {
            if (a[i] != b[i])
            {
                return (static_cast<unsigned char>(a[i]) < static_cast<unsigned char>(b[i])) ? -1 : 1;
            }
        }
        if (a.length() == b.length())
        {
            return 0;
        }
        return (a.length() < b.length()) ? -1 : 1;
    }
}

namespace ImageFile
{
    bool isNameBefore(const String &a, const String &b)
    {
        size_t i = 0;
        size_t j = 0;

        while (i < a.length() && j < b.length())
        {
            if (isDigit(a[i]) && isDigit(b[j]))
            {
                size_t aStart = 0, aLength = 0, bStart = 0, bLength = 0;
                scanDigits(a, i, aStart, aLength);
                scanDigits(b, j, bStart, bLength);

                int order = compareDigitRuns(a, aStart, aLength, b, bStart, bLength);
                if (order != 0)
                {
                    return order < 0;
                }
                continue;
            }

            char ca = toLowerAscii(a[i]);
            char cb = toLowerAscii(b[j]);
            if (ca != cb)
            {
                return static_cast<unsigned char>(ca) < static_cast<unsigned char>(cb);
            }
            i++;
            j++;
        }

        // 片方が尽きたなら短い方が前
        if ((i < a.length()) != (j < b.length()))
        {
            return j < b.length();
        }

        // ここまで同じなら、文字コードで前後を確定させる
        return compareRaw(a, b) < 0;
    }

    SortedNames::SortedNames(String *storage, int capacity)
        : _storage(storage), _capacity(capacity > 0 ? capacity : 0)
    {
    }

    bool SortedNames::insert(const String &name)
    {
        if (_storage == nullptr || _capacity <= 0)
        {
            return false;
        }

        int position = 0;
        while (position < _count && isNameBefore(_storage[position], name))
        {
            position++;
        }

        // 上限に達していて、しかも末尾より後ろなら覚える意味がない
        if (position >= _capacity)
        {
            return false;
        }

        // 満杯なら末尾を押し出す
        int from = (_count < _capacity) ? _count : _capacity - 1;
        for (int i = from; i > position; i--)
        {
            _storage[i] = _storage[i - 1];
        }
        _storage[position] = name;

        if (_count < _capacity)
        {
            _count++;
        }
        return true;
    }

    const String &SortedNames::at(int index) const
    {
        static const String empty;
        if (_storage == nullptr || index < 0 || index >= _count)
        {
            return empty;
        }
        return _storage[index];
    }
}
