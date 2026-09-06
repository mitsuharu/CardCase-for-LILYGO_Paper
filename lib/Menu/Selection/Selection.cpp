#include "Selection.h"

Selection::Selection(int itemCount, int visibleCount)
{
    this->itemCount = (itemCount > 0) ? itemCount : 0;
    this->visibleCount = (visibleCount > 0) ? visibleCount : 1;
    this->selectedIndex = 0;
}

void Selection::moveNext()
{
    if (itemCount <= 0)
    {
        return;
    }
    selectedIndex = (selectedIndex + 1) % itemCount;
}

void Selection::movePrev()
{
    if (itemCount <= 0)
    {
        return;
    }
    selectedIndex = (selectedIndex + itemCount - 1) % itemCount;
}

void Selection::select(int index)
{
    if (index < 0 || index >= itemCount)
    {
        return;
    }
    selectedIndex = index;
}

int Selection::pageIndex() const
{
    if (itemCount <= 0)
    {
        return 0;
    }
    return selectedIndex / visibleCount;
}

int Selection::pageCount() const
{
    if (itemCount <= 0)
    {
        return 0;
    }
    return (itemCount + visibleCount - 1) / visibleCount;
}

int Selection::pageStart() const
{
    return pageIndex() * visibleCount;
}

int Selection::pageLength() const
{
    if (itemCount <= 0)
    {
        return 0;
    }
    int rest = itemCount - pageStart();
    return (rest < visibleCount) ? rest : visibleCount;
}

bool Selection::isVisible(int index) const
{
    if (index < 0 || index >= itemCount)
    {
        return false;
    }
    int start = pageStart();
    return index >= start && index < start + pageLength();
}

int Selection::rowOf(int index) const
{
    if (!isVisible(index))
    {
        return -1;
    }
    return index - pageStart();
}
