#include <unity.h>
#include <ImageSize.h>
#include <vector>

void setUp(void) {}
void tearDown(void) {}

namespace
{
    void pushU16BE(std::vector<uint8_t> &out, uint16_t value)
    {
        out.push_back(static_cast<uint8_t>(value >> 8));
        out.push_back(static_cast<uint8_t>(value & 0xFF));
    }

    /// SOF0 だけを持つ最小の JPEG を組み立てる。leadingSegments は前に挟むダミー。
    std::vector<uint8_t> makeJpeg(int width, int height, size_t paddingBytes = 0)
    {
        std::vector<uint8_t> jpeg = {0xFF, 0xD8};

        if (paddingBytes > 0)
        {
            // EXIF のサムネイルのように大きな APP1 が先にある状況を模す
            jpeg.push_back(0xFF);
            jpeg.push_back(0xE1);
            pushU16BE(jpeg, static_cast<uint16_t>(paddingBytes + 2));
            jpeg.insert(jpeg.end(), paddingBytes, 0x00);
        }

        jpeg.push_back(0xFF);
        jpeg.push_back(0xC0); // SOF0
        pushU16BE(jpeg, 17);  // 長さ
        jpeg.push_back(8);    // 精度
        pushU16BE(jpeg, static_cast<uint16_t>(height));
        pushU16BE(jpeg, static_cast<uint16_t>(width));
        jpeg.insert(jpeg.end(), 10, 0x00); // 成分の記述（内容は問わない）

        return jpeg;
    }

    std::vector<uint8_t> makePng(int width, int height)
    {
        std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        png.insert(png.end(), {0x00, 0x00, 0x00, 0x0D});
        png.insert(png.end(), {'I', 'H', 'D', 'R'});
        for (int shift = 24; shift >= 0; shift -= 8)
        {
            png.push_back(static_cast<uint8_t>((width >> shift) & 0xFF));
        }
        for (int shift = 24; shift >= 0; shift -= 8)
        {
            png.push_back(static_cast<uint8_t>((height >> shift) & 0xFF));
        }
        return png;
    }
}

void test_reads_jpeg_size()
{
    std::vector<uint8_t> jpeg = makeJpeg(4032, 3024);

    int width = 0;
    int height = 0;
    TEST_ASSERT_TRUE(ImageFile::imageSize(jpeg.data(), jpeg.size(), &width, &height));
    TEST_ASSERT_EQUAL_INT(4032, width);
    TEST_ASSERT_EQUAL_INT(3024, height);
}

void test_reads_jpeg_size_after_large_exif()
{
    // EXIF にサムネイルが入っていても SOF まで辿り着けること
    std::vector<uint8_t> jpeg = makeJpeg(1200, 1600, 20000);

    int width = 0;
    int height = 0;
    TEST_ASSERT_TRUE(ImageFile::imageSize(jpeg.data(), jpeg.size(), &width, &height));
    TEST_ASSERT_EQUAL_INT(1200, width);
    TEST_ASSERT_EQUAL_INT(1600, height);
}

void test_reads_png_size()
{
    std::vector<uint8_t> png = makePng(540, 960);

    int width = 0;
    int height = 0;
    TEST_ASSERT_TRUE(ImageFile::imageSize(png.data(), png.size(), &width, &height));
    TEST_ASSERT_EQUAL_INT(540, width);
    TEST_ASSERT_EQUAL_INT(960, height);
}

void test_returns_false_for_broken_input()
{
    int width = -1;
    int height = -1;

    TEST_ASSERT_FALSE(ImageFile::imageSize(nullptr, 0, &width, &height));

    const uint8_t garbage[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    TEST_ASSERT_FALSE(ImageFile::imageSize(garbage, sizeof(garbage), &width, &height));

    // 途中で切れていても落ちないこと
    std::vector<uint8_t> jpeg = makeJpeg(640, 480);
    for (size_t size = 0; size < jpeg.size(); size++)
    {
        ImageFile::imageSize(jpeg.data(), size, &width, &height);
    }
}

void test_extension_is_decided_by_content()
{
    // 受け取った画像には名前が付いてこないので中身で判断する
    std::vector<uint8_t> jpeg = makeJpeg(640, 480);
    std::vector<uint8_t> png = makePng(640, 480);

    TEST_ASSERT_EQUAL_STRING(".jpg", ImageFile::extensionFor(jpeg.data(), jpeg.size()));
    TEST_ASSERT_EQUAL_STRING(".png", ImageFile::extensionFor(png.data(), png.size()));

    // どちらでもないものは受け付けない
    const uint8_t garbage[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    TEST_ASSERT_NULL(ImageFile::extensionFor(garbage, sizeof(garbage)));
    TEST_ASSERT_NULL(ImageFile::extensionFor(nullptr, 0));
}

int runUnityTests(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_extension_is_decided_by_content);
    RUN_TEST(test_reads_jpeg_size);
    RUN_TEST(test_reads_jpeg_size_after_large_exif);
    RUN_TEST(test_reads_png_size);
    RUN_TEST(test_returns_false_for_broken_input);
    return UNITY_END();
}

int main(void)
{
    return runUnityTests();
}
