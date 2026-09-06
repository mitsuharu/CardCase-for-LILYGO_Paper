#include <unity.h>
#include <FitBox/FitBox.h>

void setUp(void) {}
void tearDown(void) {}

namespace
{
    // T5 4.7 のパネル
    const int kPanelWidth = 960;
    const int kPanelHeight = 540;
}

void test_fits_and_centers_a_wider_image()
{
    // 4:3 の写真は 16:9 のパネルより縦が先に埋まる。左右に余白が出る。
    ImageDraw::FitBox box = ImageDraw::fitInto(4032, 3024, kPanelWidth, kPanelHeight);

    TEST_ASSERT_EQUAL_INT(720, box.width);
    TEST_ASSERT_EQUAL_INT(540, box.height);
    TEST_ASSERT_EQUAL_INT(120, box.x);
    TEST_ASSERT_EQUAL_INT(0, box.y);
}

void test_fits_a_taller_image()
{
    // 縦長の画像をそのまま出すと、横が余って上下が埋まる形になる
    ImageDraw::FitBox box = ImageDraw::fitInto(540, 960, kPanelWidth, kPanelHeight);

    TEST_ASSERT_EQUAL_INT(304, box.width);
    TEST_ASSERT_EQUAL_INT(540, box.height);
    TEST_ASSERT_EQUAL_INT(328, box.x);
    TEST_ASSERT_EQUAL_INT(0, box.y);
}

void test_exact_size_uses_the_whole_panel()
{
    ImageDraw::FitBox box = ImageDraw::fitInto(kPanelWidth, kPanelHeight, kPanelWidth, kPanelHeight);

    TEST_ASSERT_EQUAL_INT(kPanelWidth, box.width);
    TEST_ASSERT_EQUAL_INT(kPanelHeight, box.height);
    TEST_ASSERT_EQUAL_INT(0, box.x);
    TEST_ASSERT_EQUAL_INT(0, box.y);
}

void test_small_image_is_enlarged()
{
    // 名刺として見せるので、余白より大きさを取る
    ImageDraw::FitBox box = ImageDraw::fitInto(200, 200, kPanelWidth, kPanelHeight);

    TEST_ASSERT_EQUAL_INT(540, box.width);
    TEST_ASSERT_EQUAL_INT(540, box.height);
    TEST_ASSERT_EQUAL_INT(210, box.x);
}

void test_invalid_size_is_empty()
{
    ImageDraw::FitBox box = ImageDraw::fitInto(0, 0, kPanelWidth, kPanelHeight);

    TEST_ASSERT_EQUAL_INT(0, box.width);
    TEST_ASSERT_EQUAL_INT(0, box.height);
}

void test_decode_scale_never_goes_below_the_drawn_size()
{
    // 4032x3024 を 720x540 で描く。1/4 なら 1008x756 で足りるが、1/8 は 504x378 で足りない。
    TEST_ASSERT_EQUAL_INT(4, ImageDraw::decodeScaleDivisor(4032, 3024, 720, 540));

    // 十分に大きければいちばん粗く展開してよい
    TEST_ASSERT_EQUAL_INT(8, ImageDraw::decodeScaleDivisor(8000, 6000, 720, 540));

    // パネルと同じかそれ以下なら等倍
    TEST_ASSERT_EQUAL_INT(1, ImageDraw::decodeScaleDivisor(960, 540, 960, 540));
    TEST_ASSERT_EQUAL_INT(1, ImageDraw::decodeScaleDivisor(400, 300, 720, 540));

    // 画面の大きさではなく実際に描く大きさで決める。
    // 横長の画像で画面の幅を渡すと、必要以上に粗くなる。
    TEST_ASSERT_EQUAL_INT(4, ImageDraw::decodeScaleDivisor(4032, 3024, 960, 540));
}

void test_dest_span_covers_every_pixel_when_enlarging()
{
    // 100 画素を 200 画素へ引き伸ばす。元の 1 画素が描画先の 2 画素に広がる。
    ImageDraw::Span span = ImageDraw::destSpan(0, 10, 100, 0, 200);
    TEST_ASSERT_EQUAL_INT(0, span.begin);
    TEST_ASSERT_EQUAL_INT(20, span.end);

    // 次のブロックは隙間なく続く。ここがずれると縞が出る。
    ImageDraw::Span next = ImageDraw::destSpan(10, 10, 100, 0, 200);
    TEST_ASSERT_EQUAL_INT(20, next.begin);
    TEST_ASSERT_EQUAL_INT(40, next.end);
}

void test_dest_span_when_shrinking()
{
    // 200 画素を 100 画素へ縮める
    ImageDraw::Span span = ImageDraw::destSpan(0, 10, 200, 0, 100);
    TEST_ASSERT_EQUAL_INT(0, span.begin);
    TEST_ASSERT_EQUAL_INT(5, span.end);

    // 末尾のブロックは描画先の右端で止まる
    ImageDraw::Span last = ImageDraw::destSpan(190, 10, 200, 0, 100);
    TEST_ASSERT_EQUAL_INT(95, last.begin);
    TEST_ASSERT_EQUAL_INT(100, last.end);
}

void test_dest_span_respects_the_origin()
{
    // 中央に寄せたぶんだけ、描画先も右へずれる
    ImageDraw::Span span = ImageDraw::destSpan(0, 10, 100, 120, 200);
    TEST_ASSERT_EQUAL_INT(120, span.begin);
    TEST_ASSERT_EQUAL_INT(140, span.end);
}

void test_dest_span_is_empty_for_invalid_input()
{
    ImageDraw::Span span = ImageDraw::destSpan(0, 0, 100, 0, 200);
    TEST_ASSERT_EQUAL_INT(span.begin, span.end);

    ImageDraw::Span zero = ImageDraw::destSpan(0, 10, 0, 0, 200);
    TEST_ASSERT_EQUAL_INT(zero.begin, zero.end);
}

int runUnityTests(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fits_and_centers_a_wider_image);
    RUN_TEST(test_fits_a_taller_image);
    RUN_TEST(test_exact_size_uses_the_whole_panel);
    RUN_TEST(test_small_image_is_enlarged);
    RUN_TEST(test_invalid_size_is_empty);
    RUN_TEST(test_decode_scale_never_goes_below_the_drawn_size);
    RUN_TEST(test_dest_span_covers_every_pixel_when_enlarging);
    RUN_TEST(test_dest_span_when_shrinking);
    RUN_TEST(test_dest_span_respects_the_origin);
    RUN_TEST(test_dest_span_is_empty_for_invalid_input);
    return UNITY_END();
}

int main(void)
{
    return runUnityTests();
}
