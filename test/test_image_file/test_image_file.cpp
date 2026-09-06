#include <unity.h>
#include <ImageFile.h>

void setUp(void) {}
void tearDown(void) {}

void test_supported_extensions()
{
    TEST_ASSERT_TRUE(ImageFile::isSupportedImage("photo.jpg"));
    TEST_ASSERT_TRUE(ImageFile::isSupportedImage("photo.jpeg"));
    TEST_ASSERT_TRUE(ImageFile::isSupportedImage("photo.png"));

    TEST_ASSERT_FALSE(ImageFile::isSupportedImage("photo.gif"));
    TEST_ASSERT_FALSE(ImageFile::isSupportedImage("readme.txt"));
    TEST_ASSERT_FALSE(ImageFile::isSupportedImage("photo"));
    TEST_ASSERT_FALSE(ImageFile::isSupportedImage(""));
}

void test_extension_is_case_insensitive()
{
    // デジカメやスマホは大文字の拡張子を付けることがある
    TEST_ASSERT_TRUE(ImageFile::isSupportedImage("IMG_0001.JPG"));
    TEST_ASSERT_TRUE(ImageFile::isSupportedImage("IMG_0001.Jpeg"));
    TEST_ASSERT_TRUE(ImageFile::isSupportedImage("scan.PNG"));
}

void test_extension_only_name_is_not_an_image()
{
    // ".jpg" は拡張子だけで名前がなく、隠しファイルでもある
    TEST_ASSERT_TRUE(ImageFile::isHidden(".jpg"));
    TEST_ASSERT_FALSE(ImageFile::isListable(".jpg"));
}

void test_hidden_files()
{
    // macOS が SD に作るファイルを除外できること
    TEST_ASSERT_TRUE(ImageFile::isHidden("._photo.jpg"));
    TEST_ASSERT_TRUE(ImageFile::isHidden(".DS_Store"));
    TEST_ASSERT_FALSE(ImageFile::isHidden("photo.jpg"));
    TEST_ASSERT_FALSE(ImageFile::isHidden(""));
}

void test_listable()
{
    TEST_ASSERT_TRUE(ImageFile::isListable("photo.jpg"));
    TEST_ASSERT_TRUE(ImageFile::isListable("IMG_0001.JPG"));

    TEST_ASSERT_FALSE(ImageFile::isListable("._photo.jpg"));
    TEST_ASSERT_FALSE(ImageFile::isListable(".DS_Store"));
    TEST_ASSERT_FALSE(ImageFile::isListable("readme.txt"));
}

void test_root_path()
{
    TEST_ASSERT_EQUAL_STRING("/photo.jpg", ImageFile::rootPath("photo.jpg").c_str());

    // すでに絶対パスなら二重に "/" を付けない
    TEST_ASSERT_EQUAL_STRING("/photo.jpg", ImageFile::rootPath("/photo.jpg").c_str());
}

int runUnityTests(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_supported_extensions);
    RUN_TEST(test_extension_is_case_insensitive);
    RUN_TEST(test_extension_only_name_is_not_an_image);
    RUN_TEST(test_hidden_files);
    RUN_TEST(test_listable);
    RUN_TEST(test_root_path);
    return UNITY_END();
}

int main(void)
{
    return runUnityTests();
}
