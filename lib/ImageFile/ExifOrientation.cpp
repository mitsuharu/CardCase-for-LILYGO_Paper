#include "ExifOrientation.h"

namespace
{
    uint16_t readU16(const uint8_t *p, bool bigEndian)
    {
        return bigEndian
                   ? static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1])
                   : static_cast<uint16_t>((static_cast<uint16_t>(p[1]) << 8) | p[0]);
    }

    uint32_t readU32(const uint8_t *p, bool bigEndian)
    {
        if (bigEndian)
        {
            return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3];
        }
        return (static_cast<uint32_t>(p[3]) << 24) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[0];
    }

    /// EXIF 本体（TIFF 形式）から Orientation を探す
    int orientationFromTiff(const uint8_t *tiff, size_t size)
    {
        // TIFF ヘッダ: バイトオーダー(2) + 0x002A(2) + IFD0 へのオフセット(4)
        if (size < 8)
        {
            return ImageFile::kDefaultOrientation;
        }

        bool bigEndian;
        if (tiff[0] == 'I' && tiff[1] == 'I')
        {
            bigEndian = false;
        }
        else if (tiff[0] == 'M' && tiff[1] == 'M')
        {
            bigEndian = true;
        }
        else
        {
            return ImageFile::kDefaultOrientation;
        }

        if (readU16(&tiff[2], bigEndian) != 0x002A)
        {
            return ImageFile::kDefaultOrientation;
        }

        uint32_t ifdOffset = readU32(&tiff[4], bigEndian);
        if (ifdOffset > size || size - ifdOffset < 2)
        {
            return ImageFile::kDefaultOrientation;
        }

        uint16_t entryCount = readU16(&tiff[ifdOffset], bigEndian);
        for (uint16_t i = 0; i < entryCount; i++)
        {
            // 1 エントリは タグ(2) + 型(2) + 個数(4) + 値(4) の 12 バイト
            size_t entry = ifdOffset + 2 + static_cast<size_t>(i) * 12;
            if (entry + 12 > size)
            {
                break;
            }

            if (readU16(&tiff[entry], bigEndian) != 0x0112) // Orientation タグ
            {
                continue;
            }

            if (readU16(&tiff[entry + 2], bigEndian) != 3) // SHORT 以外は想定しない
            {
                break;
            }

            uint16_t value = readU16(&tiff[entry + 8], bigEndian);
            if (value >= 1 && value <= 8)
            {
                return value;
            }
            break;
        }

        return ImageFile::kDefaultOrientation;
    }
}

namespace ImageFile
{
    int exifOrientation(const uint8_t *data, size_t size)
    {
        if (data == nullptr || size < 4)
        {
            return kDefaultOrientation;
        }

        // SOI で始まらないなら JPEG ではない
        if (!(data[0] == 0xFF && data[1] == 0xD8))
        {
            return kDefaultOrientation;
        }

        static const uint8_t kExifHeader[] = {'E', 'x', 'i', 'f', 0x00, 0x00};
        const size_t kExifHeaderSize = sizeof(kExifHeader);

        size_t pos = 2;
        while (pos + 4 <= size)
        {
            if (data[pos] != 0xFF)
            {
                return kDefaultOrientation;
            }

            uint8_t marker = data[pos + 1];

            // 長さを持たないマーカー
            if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD9))
            {
                pos += 2;
                continue;
            }

            // SOS より後ろは圧縮データなので EXIF は無い
            if (marker == 0xDA)
            {
                return kDefaultOrientation;
            }

            uint16_t segmentLength = readU16(&data[pos + 2], true);
            if (segmentLength < 2)
            {
                return kDefaultOrientation;
            }

            size_t payload = pos + 4;
            size_t payloadLength = segmentLength - 2;

            if (marker == 0xE1 && payloadLength >= kExifHeaderSize && payload + kExifHeaderSize <= size)
            {
                bool isExif = true;
                for (size_t i = 0; i < kExifHeaderSize; i++)
                {
                    if (data[payload + i] != kExifHeader[i])
                    {
                        isExif = false;
                        break;
                    }
                }

                if (isExif)
                {
                    size_t tiff = payload + kExifHeaderSize;
                    size_t available = size - tiff;
                    size_t declared = payloadLength - kExifHeaderSize;
                    return orientationFromTiff(&data[tiff], (declared < available) ? declared : available);
                }
            }

            pos = payload + payloadLength;
        }

        return kDefaultOrientation;
    }

    int rotationStepsFor(int orientation)
    {
        switch (orientation)
        {
        case 3: // 180 度
        case 4: // 上下反転
            return 2;
        case 5: // 鏡像 + 270 度
        case 8: // 270 度
            return 3;
        case 6: // 90 度
        case 7: // 鏡像 + 90 度
            return 1;
        default:
            return 0;
        }
    }
}
