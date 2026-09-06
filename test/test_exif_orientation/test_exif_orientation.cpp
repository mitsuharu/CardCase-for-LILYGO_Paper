#include <unity.h>
#include <ExifOrientation.h>
#include <vector>

void setUp(void) {}
void tearDown(void) {}

namespace
{
    /// Orientation だけを持つ最小の EXIF 付き JPEG を組み立てる
    std::vector<uint8_t> makeJpegWithOrientation(int orientation, bool bigEndian)
    {
        std::vector<uint8_t> tiff;

        // TIFF ヘッダ
        if (bigEndian)
        {
            tiff.insert(tiff.end(), {'M', 'M', 0x00, 0x2A, 0x00, 0x00, 0x00, 0x08});
        }
        else
        {
            tiff.insert(tiff.end(), {'I', 'I', 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00});
        }

        auto pushU16 = [&](uint16_t value) {
            if (bigEndian)
            {
                tiff.push_back(static_cast<uint8_t>(value >> 8));
                tiff.push_back(static_cast<uint8_t>(value & 0xFF));
            }
            else
            {
                tiff.push_back(static_cast<uint8_t>(value & 0xFF));
                tiff.push_back(static_cast<uint8_t>(value >> 8));
            }
        };

        pushU16(1);      // エントリ数
        pushU16(0x0112); // Orientation タグ
        pushU16(3);      // 型: SHORT
        pushU16(1);      // 個数の下位
        pushU16(0);      // 個数の上位
        pushU16(static_cast<uint16_t>(orientation));
        pushU16(0);                                   // 値の余り
        tiff.insert(tiff.end(), {0, 0, 0, 0});        // 次の IFD なし

        std::vector<uint8_t> jpeg = {0xFF, 0xD8, 0xFF, 0xE1};

        // セグメント長 = 長さ自身(2) + "Exif\0\0"(6) + TIFF
        uint16_t segmentLength = static_cast<uint16_t>(2 + 6 + tiff.size());
        jpeg.push_back(static_cast<uint8_t>(segmentLength >> 8));
        jpeg.push_back(static_cast<uint8_t>(segmentLength & 0xFF));
        jpeg.insert(jpeg.end(), {'E', 'x', 'i', 'f', 0x00, 0x00});
        jpeg.insert(jpeg.end(), tiff.begin(), tiff.end());

        return jpeg;
    }
}

void test_reads_orientation_little_endian()
{
    // iPhone を縦に構えて撮った写真は Orientation 6 になる
    std::vector<uint8_t> jpeg = makeJpegWithOrientation(6, false);
    TEST_ASSERT_EQUAL_INT(6, ImageFile::exifOrientation(jpeg.data(), jpeg.size()));
}

void test_reads_orientation_big_endian()
{
    std::vector<uint8_t> jpeg = makeJpegWithOrientation(8, true);
    TEST_ASSERT_EQUAL_INT(8, ImageFile::exifOrientation(jpeg.data(), jpeg.size()));
}

void test_skips_leading_jfif_segment()
{
    // APP0(JFIF) が先にある写真でも APP1 まで辿れること
    std::vector<uint8_t> jpeg = makeJpegWithOrientation(6, false);

    std::vector<uint8_t> withJfif = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10,
                                     'J', 'F', 'I', 'F', 0x00, 0x01, 0x01, 0x00,
                                     0x00, 0x01, 0x00, 0x01, 0x00, 0x00};
    withJfif.insert(withJfif.end(), jpeg.begin() + 2, jpeg.end());

    TEST_ASSERT_EQUAL_INT(6, ImageFile::exifOrientation(withJfif.data(), withJfif.size()));
}

void test_returns_default_without_exif()
{
    // EXIF を持たない JPEG
    const uint8_t jpeg[] = {0xFF, 0xD8, 0xFF, 0xDB, 0x00, 0x04, 0x00, 0x00, 0xFF, 0xDA};
    TEST_ASSERT_EQUAL_INT(ImageFile::kDefaultOrientation, ImageFile::exifOrientation(jpeg, sizeof(jpeg)));
}

void test_returns_default_for_broken_input()
{
    TEST_ASSERT_EQUAL_INT(ImageFile::kDefaultOrientation, ImageFile::exifOrientation(nullptr, 0));

    // JPEG ではない
    const uint8_t png[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    TEST_ASSERT_EQUAL_INT(ImageFile::kDefaultOrientation, ImageFile::exifOrientation(png, sizeof(png)));

    // 途中で切れたファイルでも読み進めて落ちないこと
    std::vector<uint8_t> jpeg = makeJpegWithOrientation(6, false);
    for (size_t size = 0; size < jpeg.size(); size++)
    {
        int orientation = ImageFile::exifOrientation(jpeg.data(), size);
        TEST_ASSERT_TRUE(orientation >= 1 && orientation <= 8);
    }
}

void test_rejects_out_of_range_orientation()
{
    std::vector<uint8_t> jpeg = makeJpegWithOrientation(9, false);
    TEST_ASSERT_EQUAL_INT(ImageFile::kDefaultOrientation, ImageFile::exifOrientation(jpeg.data(), jpeg.size()));
}

void test_rotation_steps()
{
    // 回転なし
    TEST_ASSERT_EQUAL_INT(0, ImageFile::rotationStepsFor(1));
    TEST_ASSERT_EQUAL_INT(0, ImageFile::rotationStepsFor(2));

    // 時計回りに 90 度
    TEST_ASSERT_EQUAL_INT(1, ImageFile::rotationStepsFor(6));
    TEST_ASSERT_EQUAL_INT(1, ImageFile::rotationStepsFor(7));

    // 180 度
    TEST_ASSERT_EQUAL_INT(2, ImageFile::rotationStepsFor(3));
    TEST_ASSERT_EQUAL_INT(2, ImageFile::rotationStepsFor(4));

    // 時計回りに 270 度
    TEST_ASSERT_EQUAL_INT(3, ImageFile::rotationStepsFor(5));
    TEST_ASSERT_EQUAL_INT(3, ImageFile::rotationStepsFor(8));

    // 範囲外は回転しない
    TEST_ASSERT_EQUAL_INT(0, ImageFile::rotationStepsFor(0));
    TEST_ASSERT_EQUAL_INT(0, ImageFile::rotationStepsFor(99));
}

int runUnityTests(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_reads_orientation_little_endian);
    RUN_TEST(test_reads_orientation_big_endian);
    RUN_TEST(test_skips_leading_jfif_segment);
    RUN_TEST(test_returns_default_without_exif);
    RUN_TEST(test_returns_default_for_broken_input);
    RUN_TEST(test_rejects_out_of_range_orientation);
    RUN_TEST(test_rotation_steps);
    return UNITY_END();
}

int main(void)
{
    return runUnityTests();
}
