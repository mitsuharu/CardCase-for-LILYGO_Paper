#include "Menu.h"

#ifdef ARDUINO

#include <Board.h>
#include <Screen.h>
#include <Input.h>

namespace
{
    /// 行の文字を左に少し寄せる（反転したときに文字が枠に接しないように）
    constexpr int kRowPadding = 12;

    /// ページ送りボタンのラベル
    const char *kPrevLabel = "< Prev";
    const char *kNextLabel = "Next >";

    /// 行の上端から文字のベースラインまで
    int rowBaselineOffset()
    {
        const GFXfont *font = Screen::Fonts::row();
        int leading = Layout::kRowHeight - font->advance_y;
        return (leading > 0 ? leading / 2 : 0) + font->ascender;
    }

    int rowTop(int row)
    {
        return Layout::kListTop + row * Layout::kRowHeight;
    }
}

bool Menu::addItem(MenuItemKind kind, const String &title, const String &value)
{
    if (_itemCount >= kMaxItems)
    {
        return false;
    }
    _items[_itemCount].kind = kind;
    _items[_itemCount].title = title;
    _items[_itemCount].value = value;
    _itemCount++;
    return true;
}

bool Menu::showsPager() const
{
    return Input::hasTouch() && _selection.pageCount() > 1;
}

String Menu::operationGuide() const
{
    // ボタンは 1 つしか無いので、短押しと長押しに役割を割り当てている。
    // タッチのある機種でもこの操作は生きているので、常に出す。
    String guide = "BOOT: tap = next / hold = open";
    if (Input::hasTouch())
    {
        guide = String("Tap a name to open.   ") + guide;
    }
    return guide;
}

void Menu::begin(SelectHandler onSelect)
{
    _onSelect = onSelect;
    _selection = Selection(_itemCount, Layout::kVisibleRows);
    draw();
}

void Menu::drawRow(int index)
{
    int row = _selection.rowOf(index);
    if (row < 0)
    {
        return;
    }

    bool selected = (index == _selection.selectedIndex);
    uint8_t background = selected ? Screen::kBlack : Screen::kWhite;
    uint8_t foreground = selected ? Screen::kPaper : Screen::kInk;
    uint8_t backgroundInk = selected ? Screen::kInk : Screen::kPaper;

    int top = rowTop(row);
    Screen::fillRect(0, top, Layout::kPanelWidth, Layout::kRowHeight, background);

    const GFXfont *font = Screen::Fonts::row();
    int available = Layout::kPanelWidth - Layout::kMargin * 2 - kRowPadding;
    String title = Screen::ellipsize(font, _items[index].title, available);

    Screen::drawText(font, title.c_str(),
                     Layout::kMargin + kRowPadding, top + rowBaselineOffset(),
                     foreground, backgroundInk);
}

void Menu::drawFooter()
{
    Screen::fillRect(0, Layout::kFooterTop,
                     Layout::kPanelWidth, Layout::kPanelHeight - Layout::kFooterTop,
                     Screen::kWhite);

    const GFXfont *font = Screen::Fonts::small();

    if (_selection.pageCount() > 1)
    {
        char page[32];
        snprintf(page, sizeof(page), "page %d/%d",
                 _selection.pageIndex() + 1, _selection.pageCount());
        Screen::drawText(font, page, Layout::kMargin, Layout::kFooterLine1Baseline);
    }

    Screen::drawText(font, operationGuide().c_str(),
                     Layout::kMargin, Layout::kFooterLine2Baseline);

    if (!showsPager())
    {
        return;
    }

    Screen::drawRect(Layout::kPagerPrevX, Layout::kPagerTop,
                     Layout::kPagerWidth, Layout::kPagerHeight, Screen::kBlack);
    Screen::drawRect(Layout::kPagerNextX, Layout::kPagerTop,
                     Layout::kPagerWidth, Layout::kPagerHeight, Screen::kBlack);

    int baseline = Layout::kPagerTop + (Layout::kPagerHeight - font->advance_y) / 2 + font->ascender;
    int prevX = Layout::kPagerPrevX + (Layout::kPagerWidth - Screen::textWidth(font, kPrevLabel)) / 2;
    int nextX = Layout::kPagerNextX + (Layout::kPagerWidth - Screen::textWidth(font, kNextLabel)) / 2;

    Screen::drawText(font, kPrevLabel, prevX, baseline);
    Screen::drawText(font, kNextLabel, nextX, baseline);
}

void Menu::draw()
{
    int listHeight = Layout::kRowHeight * Layout::kVisibleRows;
    Screen::fillRect(0, Layout::kListTop, Layout::kPanelWidth, listHeight, Screen::kWhite);

    int start = _selection.pageStart();
    for (int i = 0; i < _selection.pageLength(); i++)
    {
        drawRow(start + i);
    }

    drawFooter();
}

void Menu::redrawAll()
{
    draw();
    Screen::flushArea(0, Layout::kListTop,
                      Layout::kPanelWidth, Layout::kPanelHeight - Layout::kListTop);
    Input::pauseTouch();
}

void Menu::redrawSelection(int previousIndex)
{
    int previousRow = _selection.rowOf(previousIndex);
    if (previousRow < 0)
    {
        // 前の選択が今のページに無い＝ページを跨いだので、まとめて描き直す
        redrawAll();
        return;
    }

    drawRow(previousIndex);
    drawRow(_selection.selectedIndex);

    // 動いた 2 行だけを転送する。全面だと 600ms 前後かかり、連打に追いつかない。
    Screen::flushArea(0, rowTop(previousRow), Layout::kPanelWidth, Layout::kRowHeight);
    Screen::flushArea(0, rowTop(_selection.rowOf(_selection.selectedIndex)),
                      Layout::kPanelWidth, Layout::kRowHeight);
    Input::pauseTouch();
}

void Menu::confirm()
{
    if (_onSelect == nullptr || _itemCount <= 0)
    {
        return;
    }
    _onSelect(_items[_selection.selectedIndex]);
}

bool Menu::handleTouch(int x, int y)
{
    if (showsPager() && y >= Layout::kPagerTop && y < Layout::kPagerTop + Layout::kPagerHeight)
    {
        if (x >= Layout::kPagerPrevX && x < Layout::kPagerPrevX + Layout::kPagerWidth)
        {
            int start = _selection.pageStart() - Layout::kVisibleRows;
            _selection.select(start < 0 ? 0 : start);
            redrawAll();
            return true;
        }
        if (x >= Layout::kPagerNextX && x < Layout::kPagerNextX + Layout::kPagerWidth)
        {
            int start = _selection.pageStart() + Layout::kVisibleRows;
            if (start < _itemCount)
            {
                _selection.select(start);
                redrawAll();
            }
            return true;
        }
    }

    int listBottom = Layout::kListTop + Layout::kRowHeight * Layout::kVisibleRows;
    if (y < Layout::kListTop || y >= listBottom)
    {
        return false;
    }

    int row = (y - Layout::kListTop) / Layout::kRowHeight;
    if (row < 0 || row >= _selection.pageLength())
    {
        return false;
    }

    _selection.select(_selection.pageStart() + row);
    confirm();
    return true;
}

void Menu::update()
{
    if (_itemCount <= 0)
    {
        return;
    }

    int previousIndex = _selection.selectedIndex;

    if (Input::wasLongPressed())
    {
        confirm();
        return;
    }

    if (Input::wasClicked())
    {
        _selection.moveNext();
        redrawSelection(previousIndex);
        return;
    }

    Input::Point point;
    if (Input::wasTapped(point))
    {
        handleTouch(point.x, point.y);
    }
}

#endif
