#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#include <functional>
#else
#include <string>
#include <functional>
using String = std::string;
#endif

#include "Selection/Selection.h"

/// 一覧の項目の種類
enum class MenuItemKind
{
    Image,    // SD 内の画像。value は画像のパス
    Transfer, // WiFi で画像を受け取る
};

/// 一覧に載せる 1 項目
struct MenuItem
{
    MenuItemKind kind = MenuItemKind::Image;
    String title; // 画面に表示する文字列
    String value; // 選ばれたときに呼び出し側へ渡す値（画像のパスなど）
};

#ifdef ARDUINO

/**
 * ファイル一覧の表示と選択。
 *
 * タッチのある H716 は行を直接タップして選ぶ。タッチの無い H578 / H580 では
 * ボタンだけで操作するため、選択行を白黒反転で示しながら移動する。
 * どちらの機種でも同じバイナリが動くので、カーソルは常に出す。
 *
 * 電子ペーパーの全面更新は 600ms 前後かかる。カーソルが 1 行動いただけで
 * それをやると連打に追いつかないので、動いた 2 行だけを部分更新する。
 */
class Menu
{
    typedef std::function<void(const MenuItem &item)> SelectHandler;

public:
    static const int kMaxItems = 32;

    /// 項目を追加する。上限を超えた場合は false を返す。
    bool addItem(MenuItemKind kind, const String &title, const String &value);

    int itemCount() const { return _itemCount; }

    /// 項目を空にする。SD の中身が変わったときに作り直すために使う。
    void clear() { _itemCount = 0; }

    /**
     * 一覧を組み立ててフレームバッファへ描く。
     * パネルへの転送は呼び出し側が行う（見出しとまとめて 1 回にするため）。
     */
    void begin(SelectHandler onSelect);

    /// loop() から呼ぶ。タッチとボタンの入力を処理する。
    void update();

    /// 一覧とフッタをフレームバッファへ描き直す（転送はしない）
    void draw();

private:
    MenuItem _items[kMaxItems];
    int _itemCount = 0;

    Selection _selection;
    SelectHandler _onSelect = nullptr;

    /// ページ送りのボタンを出しているか（タッチがあり、かつ複数ページのとき）
    bool showsPager() const;

    String operationGuide() const;

    void drawRow(int index);
    void drawFooter();
    void confirm();

    /// 一覧とフッタをまとめて描き直して転送する
    void redrawAll();

    /// 選択が動いたときに、動いた行だけを描き直して転送する
    void redrawSelection(int previousIndex);

    /// タッチされた位置を処理する。何かしたら true。
    bool handleTouch(int x, int y);
};

#endif
