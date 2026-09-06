#include <unity.h>
#include <ImageRotation.h>

void setUp(void) {}
void tearDown(void) {}

namespace
{
    // T5 4.7 のパネル（回転していない状態）。横長で固定。
    const int kPanelWidth = 960;
    const int kPanelHeight = 540;

    // UI と同じ向きを基準にする。この機種はフックのような事情が無いので 0。
    const int kBase = 0;

    // 画面に合わせるときに回す向き。ImageDraw が使っているのは時計回り。
    const int kCw = ImageFile::kFitStepsClockwise;
    const int kCcw = ImageFile::kFitStepsCounterClockwise;

    // スマホのカメラは縦に構えても横長のまま保存し、向きは EXIF で伝える
    const int kCameraWidth = 4032;
    const int kCameraHeight = 3024;
    const int kOrientationNone = 1;
    const int kOrientationRotate90 = 6;
}

void test_landscape_photo_fills_the_panel_without_rotating()
{
    // 横に構えて撮った写真はパネルと向きが揃っているので、回さない
    TEST_ASSERT_EQUAL_INT(0, ImageFile::displayRotation(kOrientationNone, kCameraWidth, kCameraHeight,
                                                        kBase, kPanelWidth, kPanelHeight, kCw));
}

void test_portrait_photo_is_rotated_to_fill_the_landscape_panel()
{
    // 縦に構えて撮った写真は、EXIF の 90 度でいったん縦長になる。
    // パネルは横長なので、そのままだと細い帯になってしまう。もう 90 度回して埋める。
    TEST_ASSERT_EQUAL_INT(2, ImageFile::displayRotation(kOrientationRotate90, kCameraWidth, kCameraHeight,
                                                        kBase, kPanelWidth, kPanelHeight, kCw));

    // 反時計回りにすると 1 + 3 = 4 → 0。どちらでも画面は埋まる。
    TEST_ASSERT_EQUAL_INT(0, ImageFile::displayRotation(kOrientationRotate90, kCameraWidth, kCameraHeight,
                                                        kBase, kPanelWidth, kPanelHeight, kCcw));
}

void test_portrait_pixels_without_exif_are_rotated()
{
    // 縦長のまま保存された画像（スクリーンショットなど）も、パネルに合わせて回す
    TEST_ASSERT_EQUAL_INT(kCw, ImageFile::displayRotation(kOrientationNone, 540, 960,
                                                          kBase, kPanelWidth, kPanelHeight, kCw));
}

void test_panel_sized_image_keeps_base_rotation()
{
    // パネルぴったりに作った画像は回さない
    TEST_ASSERT_EQUAL_INT(kBase, ImageFile::displayRotation(kOrientationNone, kPanelWidth, kPanelHeight,
                                                            kBase, kPanelWidth, kPanelHeight, kCw));
}

void test_square_image_is_not_rotated()
{
    // 正方形はどちらの向きにも当てはまらない。回しても得しないのでそのまま。
    TEST_ASSERT_EQUAL_INT(kBase, ImageFile::displayRotation(kOrientationNone, 1000, 1000,
                                                            kBase, kPanelWidth, kPanelHeight, kCw));
}

void test_unknown_size_falls_back_to_exif_only()
{
    // 寸法が読めなければ EXIF の向きだけで決める
    TEST_ASSERT_EQUAL_INT(1, ImageFile::displayRotation(kOrientationRotate90, 0, 0,
                                                        kBase, kPanelWidth, kPanelHeight, kCw));

    TEST_ASSERT_EQUAL_INT(kBase, ImageFile::displayRotation(kOrientationNone, 0, 0,
                                                            kBase, kPanelWidth, kPanelHeight, kCw));
}

void test_fit_steps_follow_the_requested_direction()
{
    // 縦長の画像を横長のパネルに出すときだけ回る。向きは指定どおり。
    TEST_ASSERT_EQUAL_INT(kCw, ImageFile::orientationFitSteps(3024, 4032, kPanelWidth, kPanelHeight, kCw));
    TEST_ASSERT_EQUAL_INT(kCcw, ImageFile::orientationFitSteps(3024, 4032, kPanelWidth, kPanelHeight, kCcw));

    // 横長どうしはそのまま
    TEST_ASSERT_EQUAL_INT(0, ImageFile::orientationFitSteps(4032, 3024, kPanelWidth, kPanelHeight, kCw));

    // 正方形は回さない
    TEST_ASSERT_EQUAL_INT(0, ImageFile::orientationFitSteps(1000, 1000, kPanelWidth, kPanelHeight, kCw));

    // 寸法が取れなかった場合は回さない
    TEST_ASSERT_EQUAL_INT(0, ImageFile::orientationFitSteps(0, 0, kPanelWidth, kPanelHeight, kCw));
}

int runUnityTests(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_landscape_photo_fills_the_panel_without_rotating);
    RUN_TEST(test_portrait_photo_is_rotated_to_fill_the_landscape_panel);
    RUN_TEST(test_portrait_pixels_without_exif_are_rotated);
    RUN_TEST(test_panel_sized_image_keeps_base_rotation);
    RUN_TEST(test_square_image_is_not_rotated);
    RUN_TEST(test_unknown_size_falls_back_to_exif_only);
    RUN_TEST(test_fit_steps_follow_the_requested_direction);
    return UNITY_END();
}

int main(void)
{
    return runUnityTests();
}
