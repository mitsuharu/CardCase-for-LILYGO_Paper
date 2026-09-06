#include "ImageSize.h"

namespace
{
    uint16_t readU16BE(const uint8_t *p)
    {
        return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
    }

    uint32_t readU32BE(const uint8_t *p)
    {
        return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3];
    }

    /// SOF（フレームヘッダ）のマーカーか。DHT / DAC / RST は同じ範囲にあるので除く。
    bool isStartOfFrame(uint8_t marker)
    {
        if (marker < 0xC0 || marker > 0xCF)
        {
            return false;
        }
        return marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
    }

    bool jpegSize(const uint8_t *data, size_t size, int *width, int *height)
    {
        size_t pos = 2; // SOI の次から
        while (pos + 4 <= size)
        {
            if (data[pos] != 0xFF)
            {
                return false;
            }

            uint8_t marker = data[pos + 1];

            // 長さを持たないマーカー
            if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD9))
            {
                pos += 2;
                continue;
            }

            uint16_t segmentLength = readU16BE(&data[pos + 2]);
            if (segmentLength < 2)
            {
                return false;
            }

            if (isStartOfFrame(marker))
            {
                // 精度(1) + 高さ(2) + 幅(2)
                if (segmentLength < 7 || pos + 9 > size)
                {
                    return false;
                }
                *height = readU16BE(&data[pos + 5]);
                *width = readU16BE(&data[pos + 7]);
                return (*width > 0 && *height > 0);
            }

            // SOS より後ろは圧縮データなので、ここまでに SOF が無ければ諦める
            if (marker == 0xDA)
            {
                return false;
            }

            pos += 2 + segmentLength;
        }
        return false;
    }

    bool pngSize(const uint8_t *data, size_t size, int *width, int *height)
    {
        // シグネチャ(8) + 長さ(4) + "IHDR"(4) + 幅(4) + 高さ(4)
        if (size < 24)
        {
            return false;
        }
        if (!(data[12] == 'I' && data[13] == 'H' && data[14] == 'D' && data[15] == 'R'))
        {
            return false;
        }

        uint32_t w = readU32BE(&data[16]);
        uint32_t h = readU32BE(&data[20]);
        if (w == 0 || h == 0 || w > INT16_MAX || h > INT16_MAX)
        {
            return false;
        }

        *width = static_cast<int>(w);
        *height = static_cast<int>(h);
        return true;
    }
}

namespace ImageFile
{
    const char *extensionFor(const uint8_t *data, size_t size)
    {
        if (data == nullptr || size < 8)
        {
            return nullptr;
        }
        if (data[0] == 0xFF && data[1] == 0xD8)
        {
            return ".jpg";
        }

        static const uint8_t kPngSignature[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        for (size_t i = 0; i < sizeof(kPngSignature); i++)
        {
            if (data[i] != kPngSignature[i])
            {
                return nullptr;
            }
        }
        return ".png";
    }

    bool imageSize(const uint8_t *data, size_t size, int *width, int *height)
    {
        if (data == nullptr || width == nullptr || height == nullptr || size < 8)
        {
            return false;
        }

        if (data[0] == 0xFF && data[1] == 0xD8)
        {
            return jpegSize(data, size, width, height);
        }

        static const uint8_t kPngSignature[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        for (size_t i = 0; i < sizeof(kPngSignature); i++)
        {
            if (data[i] != kPngSignature[i])
            {
                return false;
            }
        }
        return pngSize(data, size, width, height);
    }
}
