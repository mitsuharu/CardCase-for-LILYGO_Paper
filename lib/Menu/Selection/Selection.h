#pragma once

/**
 * 一覧の選択位置とページングの計算。
 *
 * 描画も入力も持たない純粋なロジックなので native 環境で単体テストする。
 * ボタンが 1 つしか無く「次へ」だけで全項目を回せる必要があるため、
 * 移動は端で折り返す。
 */
struct Selection
{
    /// 項目の総数
    int itemCount = 0;

    /// 1 ページに表示できる件数
    int visibleCount = 1;

    /// 現在選択している項目（0 起点）
    int selectedIndex = 0;

    Selection() = default;
    Selection(int itemCount, int visibleCount);

    /// 選択を 1 つ進める。末尾では先頭に折り返す。
    void moveNext();

    /// 選択を 1 つ戻す。先頭では末尾に折り返す。
    void movePrev();

    /// 選択位置を直接指定する。範囲外は無視する。
    void select(int index);

    /// 選択中の項目が属するページ（0 起点）
    int pageIndex() const;

    /// ページ数
    int pageCount() const;

    /// 現在のページの先頭の項目番号
    int pageStart() const;

    /// 現在のページに表示される件数
    int pageLength() const;

    /// index が現在のページに表示されているか
    bool isVisible(int index) const;

    /// 現在のページ内での行番号。表示されていない場合は -1。
    int rowOf(int index) const;
};
