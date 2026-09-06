#include <unity.h>
#include <Selection/Selection.h>

void setUp(void) {}
void tearDown(void) {}

void test_move_next_wraps_at_the_end()
{
    // ボタンが 1 つしかなく「次へ」だけで全項目を回すため折り返しが要る
    Selection selection(3, 10);

    TEST_ASSERT_EQUAL_INT(0, selection.selectedIndex);
    selection.moveNext();
    TEST_ASSERT_EQUAL_INT(1, selection.selectedIndex);
    selection.moveNext();
    TEST_ASSERT_EQUAL_INT(2, selection.selectedIndex);
    selection.moveNext();
    TEST_ASSERT_EQUAL_INT(0, selection.selectedIndex);
}

void test_move_prev_wraps_at_the_beginning()
{
    Selection selection(3, 10);

    selection.movePrev();
    TEST_ASSERT_EQUAL_INT(2, selection.selectedIndex);
    selection.movePrev();
    TEST_ASSERT_EQUAL_INT(1, selection.selectedIndex);
}

void test_select_ignores_out_of_range()
{
    Selection selection(3, 10);

    selection.select(2);
    TEST_ASSERT_EQUAL_INT(2, selection.selectedIndex);

    selection.select(3);
    TEST_ASSERT_EQUAL_INT(2, selection.selectedIndex);

    selection.select(-1);
    TEST_ASSERT_EQUAL_INT(2, selection.selectedIndex);
}

void test_paging()
{
    // 7 件を 3 件ずつ表示 → 3 ページ（3 + 3 + 1）
    Selection selection(7, 3);

    TEST_ASSERT_EQUAL_INT(3, selection.pageCount());

    selection.select(0);
    TEST_ASSERT_EQUAL_INT(0, selection.pageIndex());
    TEST_ASSERT_EQUAL_INT(0, selection.pageStart());
    TEST_ASSERT_EQUAL_INT(3, selection.pageLength());

    selection.select(4);
    TEST_ASSERT_EQUAL_INT(1, selection.pageIndex());
    TEST_ASSERT_EQUAL_INT(3, selection.pageStart());
    TEST_ASSERT_EQUAL_INT(3, selection.pageLength());

    // 最終ページは端数の 1 件だけ
    selection.select(6);
    TEST_ASSERT_EQUAL_INT(2, selection.pageIndex());
    TEST_ASSERT_EQUAL_INT(6, selection.pageStart());
    TEST_ASSERT_EQUAL_INT(1, selection.pageLength());
}

void test_visibility_and_row()
{
    Selection selection(7, 3);
    selection.select(4);

    // 2 ページ目には 3,4,5 が並ぶ
    TEST_ASSERT_FALSE(selection.isVisible(2));
    TEST_ASSERT_TRUE(selection.isVisible(3));
    TEST_ASSERT_TRUE(selection.isVisible(5));
    TEST_ASSERT_FALSE(selection.isVisible(6));

    TEST_ASSERT_EQUAL_INT(0, selection.rowOf(3));
    TEST_ASSERT_EQUAL_INT(1, selection.rowOf(4));
    TEST_ASSERT_EQUAL_INT(-1, selection.rowOf(6));
}

void test_empty_selection_is_safe()
{
    Selection selection(0, 5);

    selection.moveNext();
    selection.movePrev();

    TEST_ASSERT_EQUAL_INT(0, selection.selectedIndex);
    TEST_ASSERT_EQUAL_INT(0, selection.pageCount());
    TEST_ASSERT_EQUAL_INT(0, selection.pageLength());
    TEST_ASSERT_FALSE(selection.isVisible(0));
}

int runUnityTests(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_move_next_wraps_at_the_end);
    RUN_TEST(test_move_prev_wraps_at_the_beginning);
    RUN_TEST(test_select_ignores_out_of_range);
    RUN_TEST(test_paging);
    RUN_TEST(test_visibility_and_row);
    RUN_TEST(test_empty_selection_is_safe);
    return UNITY_END();
}

int main(void)
{
    return runUnityTests();
}
