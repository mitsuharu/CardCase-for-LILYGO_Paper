#pragma once

/**
 * 画像を画面に収めるときの寸法の計算。
 *
 * デコードも描画も持たない純粋なロジックなので native 環境で単体テストする。
 * 割り算の丸めがずれると、右端や下端に 1 px の隙間ができたり、逆にはみ出したりする。
 */
namespace ImageDraw
{
    /// 描画先の矩形（論理座標）
    struct FitBox
    {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
    };

    /**
     * 縦横比を保ったまま画面に収まる大きさを求め、中央に寄せる。
     * 画面より小さい画像は引き伸ばす（名刺として見せるので余白より大きさを取る）。
     */
    FitBox fitInto(int srcWidth, int srcHeight, int destWidth, int destHeight);

    /**
     * JPEG をデコードするときの縮小率（1 / 2 / 4 / 8）を返す。
     *
     * JPEGDEC は 1/2・1/4・1/8 で展開できる。表示する大きさを下回らない範囲で
     * いちばん粗く展開すると、時間もメモリも減らせる。
     *
     * 引数の fitWidth / fitHeight は fitInto() が返した実際に描く大きさ。
     * 画面の大きさをそのまま渡すと、横長の画像で必要以上に粗くなる。
     */
    int decodeScaleDivisor(int srcWidth, int srcHeight, int fitWidth, int fitHeight);

    /// 半開区間 [begin, end)
    struct Span
    {
        int begin = 0;
        int end = 0;
    };

    /**
     * 元画像の [srcBegin, srcBegin + srcCount) を写す先の範囲を返す。
     *
     * デコーダはブロック単位・行単位でしか渡してこないので、
     * 「このブロックが受け持つ描画先はどこか」を毎回求める必要がある。
     *
     * 描画先から元画像を引く向き（dest → src）で範囲を決めているので、
     * 拡大しても隙間ができない。src → dest で回すと拡大時に穴が空く。
     */
    Span destSpan(int srcBegin, int srcCount, int srcTotal, int destOrigin, int destSize);
}
