#include <unity.h>
#include <ImageOrder.h>

void setUp(void) {}
void tearDown(void) {}

void test_orders_by_name()
{
    TEST_ASSERT_TRUE(ImageFile::isNameBefore("apple.png", "banana.png"));
    TEST_ASSERT_FALSE(ImageFile::isNameBefore("banana.png", "apple.png"));
}

void test_ignores_case()
{
    // デジカメの IMG_0001.JPG と手で付けた img.png が離れて並ばないようにする
    TEST_ASSERT_TRUE(ImageFile::isNameBefore("apple.png", "Banana.png"));
    TEST_ASSERT_TRUE(ImageFile::isNameBefore("Apple.png", "banana.png"));
}

void test_compares_digits_as_numbers()
{
    // 文字コード順だと card10 が card2 より前にきて、並んでいないように見える
    TEST_ASSERT_TRUE(ImageFile::isNameBefore("card2.png", "card10.png"));
    TEST_ASSERT_FALSE(ImageFile::isNameBefore("card10.png", "card2.png"));

    TEST_ASSERT_TRUE(ImageFile::isNameBefore("IMG_0009.JPG", "IMG_0010.JPG"));
    TEST_ASSERT_TRUE(ImageFile::isNameBefore("IMG_99.JPG", "IMG_100.JPG"));
}

void test_leading_zeros_do_not_change_the_value()
{
    // 桁揃えの 0 は値を変えない。並びは後ろの文字で決まる。
    TEST_ASSERT_TRUE(ImageFile::isNameBefore("a007b.png", "a7c.png"));
    TEST_ASSERT_FALSE(ImageFile::isNameBefore("a7c.png", "a007b.png"));

    // 値が同じで名前も同じ長さなら、最後は文字コードで決める
    TEST_ASSERT_TRUE(ImageFile::isNameBefore("a07.png", "a7.png"));
}

void test_very_long_digit_runs_do_not_overflow()
{
    // 数値に変換していたら溢れる長さ。並びとして比べているので影響を受けない。
    TEST_ASSERT_TRUE(ImageFile::isNameBefore(
        "00000000000000000000000000000001.jpg",
        "00000000000000000000000000000002.jpg"));
    TEST_ASSERT_TRUE(ImageFile::isNameBefore(
        "99999999999999999999.jpg",
        "999999999999999999999.jpg"));
}

void test_shorter_name_comes_first_when_prefix_matches()
{
    TEST_ASSERT_TRUE(ImageFile::isNameBefore("card.png", "cardcase.png"));
}

void test_order_is_stable_for_case_only_differences()
{
    // 大文字小文字だけの違いでも前後が決まる（入力の順に左右されない）
    bool forward = ImageFile::isNameBefore("A.png", "a.png");
    bool backward = ImageFile::isNameBefore("a.png", "A.png");
    TEST_ASSERT_NOT_EQUAL(forward, backward);
}

void test_sorted_names_keeps_order()
{
    String storage[8];
    ImageFile::SortedNames names(storage, 8);

    names.insert("card10.png");
    names.insert("apple.png");
    names.insert("card2.png");
    names.insert("Banana.png");

    TEST_ASSERT_EQUAL_INT(4, names.count());
    TEST_ASSERT_EQUAL_STRING("apple.png", names.at(0).c_str());
    TEST_ASSERT_EQUAL_STRING("Banana.png", names.at(1).c_str());
    TEST_ASSERT_EQUAL_STRING("card2.png", names.at(2).c_str());
    TEST_ASSERT_EQUAL_STRING("card10.png", names.at(3).c_str());
}

void test_sorted_names_keeps_the_first_ones_when_full()
{
    // 上限に達したあとで前に入る名前が出てきても取りこぼさない。
    // SD の走査順は名前順ではないので、ここが効く。
    String storage[3];
    ImageFile::SortedNames names(storage, 3);

    names.insert("d.png");
    names.insert("e.png");
    names.insert("f.png");
    TEST_ASSERT_EQUAL_INT(3, names.count());

    TEST_ASSERT_TRUE(names.insert("a.png"));
    TEST_ASSERT_EQUAL_INT(3, names.count());
    TEST_ASSERT_EQUAL_STRING("a.png", names.at(0).c_str());
    TEST_ASSERT_EQUAL_STRING("d.png", names.at(1).c_str());
    TEST_ASSERT_EQUAL_STRING("e.png", names.at(2).c_str());

    // 末尾より後ろなら捨てる
    TEST_ASSERT_FALSE(names.insert("z.png"));
    TEST_ASSERT_EQUAL_INT(3, names.count());
    TEST_ASSERT_EQUAL_STRING("e.png", names.at(2).c_str());
}

void test_sorted_names_is_safe_when_empty()
{
    String storage[2];
    ImageFile::SortedNames names(storage, 2);

    TEST_ASSERT_EQUAL_INT(0, names.count());
    TEST_ASSERT_EQUAL_STRING("", names.at(0).c_str());
    TEST_ASSERT_EQUAL_STRING("", names.at(-1).c_str());

    ImageFile::SortedNames none(nullptr, 0);
    TEST_ASSERT_FALSE(none.insert("a.png"));
    TEST_ASSERT_EQUAL_INT(0, none.count());
}

int runUnityTests(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_orders_by_name);
    RUN_TEST(test_ignores_case);
    RUN_TEST(test_compares_digits_as_numbers);
    RUN_TEST(test_leading_zeros_do_not_change_the_value);
    RUN_TEST(test_very_long_digit_runs_do_not_overflow);
    RUN_TEST(test_shorter_name_comes_first_when_prefix_matches);
    RUN_TEST(test_order_is_stable_for_case_only_differences);
    RUN_TEST(test_sorted_names_keeps_order);
    RUN_TEST(test_sorted_names_keeps_the_first_ones_when_full);
    RUN_TEST(test_sorted_names_is_safe_when_empty);
    return UNITY_END();
}

int main(void)
{
    return runUnityTests();
}
