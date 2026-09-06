#pragma once

/**
 * 一覧に並べる順番。
 *
 * SD の走査は `openNextFile()` の順、つまり FAT に登録された順（おおむね
 * 書き込んだ順）で返ってくる。カードに入れた順で並ぶと、探すのに毎回
 * 全部を見ることになるので、名前順に並べ替える。
 *
 * 比較も並べ替えも Arduino に依存しない純粋ロジックなので native 環境で
 * 単体テストする。
 */

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <string>
using String = std::string;
#endif

namespace ImageFile
{
    /**
     * 名前順で a が b より前にくるか。
     *
     * Finder や Explorer と同じ感覚になるよう、2 点だけ単純な文字コード順から外す。
     *
     *   1. 大文字小文字を区別しない。デジカメの `IMG_0001.JPG` と手で付けた
     *      `img.png` が離れた場所に並ぶのを避ける
     *   2. 数字は数値として比べる。文字コード順だと `card10` が `card2` より
     *      前にきて、並んでいないように見える
     *
     * 同じ位置と見なされた場合は、最後に文字コードで比べて順番を確定させる。
     * そうしないと `A.png` と `a.png` の前後が入力の順に左右される。
     */
    bool isNameBefore(const String &a, const String &b);

    /**
     * 名前順を保ったまま、決まった数だけ覚えておく入れもの。
     *
     * 一覧に載せられる数には上限がある（`Menu::kMaxItems`）。全部を読んでから
     * 並べ替えて切ると、カードの中身が多いときにその分だけメモリを使う。
     * 入れるたびに位置を決めて、あふれたら末尾を捨てることで、**名前順で
     * 先頭から数えた分**が常に残るようにする。上限に達したあとで前に入る名前が
     * 出てきても取りこぼさない。
     *
     * 記憶領域は呼び出し側が渡す。ここでは確保も解放もしない。
     */
    class SortedNames
    {
    public:
        SortedNames(String *storage, int capacity);

        /// 名前を入れる。入れば true、上限より後ろで捨てたら false。
        bool insert(const String &name);

        int count() const { return _count; }

        /// 範囲外を渡した場合は空文字を返す
        const String &at(int index) const;

    private:
        String *_storage;
        int _capacity;
        int _count = 0;
    };
}
