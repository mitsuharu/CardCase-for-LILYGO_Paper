#include "ImageDraw.h"

#ifdef ARDUINO

#include <new>
#include <JPEGDEC.h>
#include <PNGdec.h>

#include <Screen.h>
#include <Storage.h>
#include <Board.h>
#include <ExifOrientation.h>
#include <ImageSize.h>
#include <ImageRotation.h>
#include "FitBox/FitBox.h"

namespace
{
    using ImageDraw::FitBox;
    using ImageDraw::Span;

    /**
     * 画像と画面の向きが食い違うときに、どちら向きへ回すか。
     *
     * どちらでも画面には収まるので、本体を持ち替える向きの好みで決まる。
     * 実機で逆に感じたら kFitStepsCounterClockwise に変えるだけでよい。
     */
    constexpr int kFitSteps = ImageFile::kFitStepsClockwise;

    // EXIF にサムネイルが入っていると SOF は数十 KB 先になるので広めに読む
    constexpr size_t kHeaderSize = 64 * 1024;

    /**
     * 4×4 の組織的ディザ（Bayer）。
     *
     * パネルは 16 階調しか出せないので、8bit のまま切り捨てると空や肌に
     * はっきりした縞が出る。誤差拡散は行を跨いで誤差を持ち回る必要があり、
     * デコーダがブロック単位・行単位でしか渡してこない今の作りでは使えない。
     * 位置だけで決まるこの方式なら、渡ってくる順番に関係なく同じ結果になる。
     */
    constexpr uint8_t kBayer[16] = {
        0, 8, 2, 10,
        12, 4, 14, 6,
        3, 11, 1, 9,
        15, 7, 13, 5};

    /// 1 階調ぶん（255 / 15）の中で揺らして、切り捨ての段差をほぐす
    inline uint8_t dither(int x, int y, int gray)
    {
        int noise = (kBayer[(y & 3) * 4 + (x & 3)] * 17) / 16 - 8;
        int value = gray + noise;
        if (value < 0)
        {
            return 0;
        }
        if (value > 255)
        {
            return 255;
        }
        return static_cast<uint8_t>(value);
    }

    /// RGB565 から輝度を出す
    inline uint8_t luminance565(uint16_t pixel)
    {
        int r = ((pixel >> 11) & 0x1F) << 3;
        int g = ((pixel >> 5) & 0x3F) << 2;
        int b = (pixel & 0x1F) << 3;
        return static_cast<uint8_t>((r * 77 + g * 150 + b * 29) >> 8);
    }

    /// デコーダから渡ってきたブロックを描画先へ写すための情報
    struct RenderContext
    {
        FitBox box;
        int sourceWidth = 0;  // デコード後（縮小後）の大きさ
        int sourceHeight = 0;
        uint16_t *line = nullptr; // PNG の 1 行を RGB565 で受ける
        PNG *png = nullptr;
    };

    /// 描画先の点が参照する元画像の位置
    inline int sourceIndex(int dest, int destOrigin, int destSize, int sourceTotal)
    {
        int index = static_cast<int>((static_cast<long long>(dest - destOrigin) * sourceTotal) / destSize);
        if (index < 0)
        {
            return 0;
        }
        if (index >= sourceTotal)
        {
            return sourceTotal - 1;
        }
        return index;
    }

    // ---- JPEG ----------------------------------------------------------------

    int onJpegDraw(JPEGDRAW *pDraw)
    {
        RenderContext *ctx = static_cast<RenderContext *>(pDraw->pUser);
        const uint8_t *pixels = reinterpret_cast<const uint8_t *>(pDraw->pPixels);

        int pitch = pDraw->iWidth;
        int usable = (pDraw->iWidthUsed > 0 && pDraw->iWidthUsed < pitch) ? pDraw->iWidthUsed : pitch;

        Span rows = ImageDraw::destSpan(pDraw->y, pDraw->iHeight, ctx->sourceHeight,
                                        ctx->box.y, ctx->box.height);
        Span columns = ImageDraw::destSpan(pDraw->x, usable, ctx->sourceWidth,
                                           ctx->box.x, ctx->box.width);

        for (int dy = rows.begin; dy < rows.end; dy++)
        {
            int sy = sourceIndex(dy, ctx->box.y, ctx->box.height, ctx->sourceHeight) - pDraw->y;
            if (sy < 0)
            {
                sy = 0;
            }
            if (sy >= pDraw->iHeight)
            {
                sy = pDraw->iHeight - 1;
            }

            const uint8_t *row = pixels + static_cast<size_t>(sy) * pitch;
            for (int dx = columns.begin; dx < columns.end; dx++)
            {
                int sx = sourceIndex(dx, ctx->box.x, ctx->box.width, ctx->sourceWidth) - pDraw->x;
                if (sx < 0)
                {
                    sx = 0;
                }
                if (sx >= usable)
                {
                    sx = usable - 1;
                }
                Screen::putGray(dx, dy, dither(dx, dy, row[sx]));
            }
        }
        return 1;
    }

    int32_t onJpegRead(JPEGFILE *handle, uint8_t *buffer, int32_t length)
    {
        File *file = static_cast<File *>(handle->fHandle);
        int32_t remain = handle->iSize - handle->iPos;
        if (length > remain)
        {
            length = remain;
        }
        if (length <= 0)
        {
            return 0;
        }
        int32_t read = file->read(buffer, length);
        handle->iPos = file->position();
        return read;
    }

    int32_t onJpegSeek(JPEGFILE *handle, int32_t position)
    {
        File *file = static_cast<File *>(handle->fHandle);
        file->seek(position);
        handle->iPos = file->position();
        return handle->iPos;
    }

    /// ファイルはこちらで開閉するので、デコーダからの依頼では何もしない
    void onJpegClose(void *) {}

    bool decodeJpeg(JPEGDEC &jpeg, RenderContext &ctx, int divisor)
    {
        jpeg.setPixelType(EIGHT_BIT_GRAYSCALE);
        jpeg.setUserPointer(&ctx);

        // 展開後の大きさは縮小率で決まる。写し込みの計算はこちらを基準にする。
        ctx.sourceWidth = jpeg.getWidth() / divisor;
        ctx.sourceHeight = jpeg.getHeight() / divisor;
        if (ctx.sourceWidth <= 0 || ctx.sourceHeight <= 0)
        {
            return false;
        }

        int options = 0;
        switch (divisor)
        {
        case 8:
            options = JPEG_SCALE_EIGHTH;
            break;
        case 4:
            options = JPEG_SCALE_QUARTER;
            break;
        case 2:
            options = JPEG_SCALE_HALF;
            break;
        default:
            options = 0;
            break;
        }

        return jpeg.decode(0, 0, options) == 1;
    }

    // ---- PNG -----------------------------------------------------------------

    // PNG のデコーダはファイル名を渡す形しか無いので、開いたファイルをここで持つ
    File pngFile;

    void *onPngOpen(const char *name, int32_t *size)
    {
        pngFile = Storage::fs().open(name, FILE_READ);
        if (!pngFile)
        {
            return nullptr;
        }
        *size = pngFile.size();
        return &pngFile;
    }

    void onPngClose(void *)
    {
        if (pngFile)
        {
            pngFile.close();
        }
    }

    int32_t onPngRead(PNGFILE *handle, uint8_t *buffer, int32_t length)
    {
        File *file = static_cast<File *>(handle->fHandle);
        int32_t remain = handle->iSize - handle->iPos;
        if (length > remain)
        {
            length = remain;
        }
        if (length <= 0)
        {
            return 0;
        }
        int32_t read = file->read(buffer, length);
        handle->iPos = file->position();
        return read;
    }

    int32_t onPngSeek(PNGFILE *handle, int32_t position)
    {
        File *file = static_cast<File *>(handle->fHandle);
        file->seek(position);
        handle->iPos = file->position();
        return handle->iPos;
    }

    int onPngDraw(PNGDRAW *pDraw)
    {
        RenderContext *ctx = static_cast<RenderContext *>(pDraw->pUser);
        if (ctx->line == nullptr || ctx->png == nullptr)
        {
            return 0;
        }

        // この行がどの描画行にも当たらないなら、展開の手間を省く
        Span rows = ImageDraw::destSpan(pDraw->y, 1, ctx->sourceHeight, ctx->box.y, ctx->box.height);
        if (rows.begin >= rows.end)
        {
            return 1;
        }

        // 透過は白で埋める。電子ペーパーの地の色に合わせる。
        ctx->png->getLineAsRGB565(pDraw, ctx->line, PNG_RGB565_LITTLE_ENDIAN, 0x00FFFFFF);

        for (int dy = rows.begin; dy < rows.end; dy++)
        {
            for (int dx = ctx->box.x; dx < ctx->box.x + ctx->box.width; dx++)
            {
                int sx = sourceIndex(dx, ctx->box.x, ctx->box.width, ctx->sourceWidth);
                if (sx >= pDraw->iWidth)
                {
                    sx = pDraw->iWidth - 1;
                }
                Screen::putGray(dx, dy, dither(dx, dy, luminance565(ctx->line[sx])));
            }
        }
        return 1;
    }

    // ---- 共通 ----------------------------------------------------------------

    bool isPng(const uint8_t *data, size_t size)
    {
        static const uint8_t kSignature[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        if (data == nullptr || size < sizeof(kSignature))
        {
            return false;
        }
        for (size_t i = 0; i < sizeof(kSignature); i++)
        {
            if (data[i] != kSignature[i])
            {
                return false;
            }
        }
        return true;
    }

    /// 画像の先頭バイト列から、表示するときの画面の回転を決める
    int rotationForHeader(const uint8_t *data, size_t size)
    {
        int orientation = ImageFile::exifOrientation(data, size);

        int imageWidth = 0;
        int imageHeight = 0;
        if (!ImageFile::imageSize(data, size, &imageWidth, &imageHeight))
        {
            imageWidth = 0;
            imageHeight = 0;
        }

        // 画面の大きさは回転していない状態（960×540）で渡す。
        // 画面を回しても本体の向きは変わらないため。
        return ImageFile::displayRotation(orientation, imageWidth, imageHeight,
                                          0, Layout::kPanelWidth, Layout::kPanelHeight,
                                          kFitSteps);
    }

    /// ファイルの先頭を読んで回転を決める
    int rotationForFile(const String &path)
    {
        File file = Storage::fs().open(path.c_str(), FILE_READ);
        if (!file)
        {
            return 0;
        }

        size_t size = file.size();
        if (size > kHeaderSize)
        {
            size = kHeaderSize;
        }

        uint8_t *buffer = static_cast<uint8_t *>(ps_malloc(size));
        if (buffer == nullptr)
        {
            buffer = static_cast<uint8_t *>(malloc(size));
        }
        if (buffer == nullptr)
        {
            file.close();
            return 0;
        }

        size_t read = file.read(buffer, size);
        file.close();

        int rotation = rotationForHeader(buffer, read);
        free(buffer);
        return rotation;
    }

    bool acceptableSize(int width, int height)
    {
        return width > 0 && height > 0 &&
               width <= ImageDraw::kMaxSourceWidth && height <= ImageDraw::kMaxSourceHeight;
    }

    /// デコーダを起こす前に、描画先と縮小率を決める
    bool prepare(RenderContext &ctx, int sourceWidth, int sourceHeight, int &divisor)
    {
        if (!acceptableSize(sourceWidth, sourceHeight))
        {
            log_w("unsupported image size: %dx%d", sourceWidth, sourceHeight);
            return false;
        }

        ctx.box = ImageDraw::fitInto(sourceWidth, sourceHeight, Screen::width(), Screen::height());
        if (ctx.box.width <= 0 || ctx.box.height <= 0)
        {
            return false;
        }

        divisor = ImageDraw::decodeScaleDivisor(sourceWidth, sourceHeight, ctx.box.width, ctx.box.height);
        return true;
    }
}

namespace ImageDraw
{
    bool drawFile(const String &path)
    {
        if (!Storage::isAvailable())
        {
            return false;
        }

        Screen::setRotation(rotationForFile(path));
        Screen::clear(Screen::kWhite);

        String lowered = path;
        lowered.toLowerCase();
        bool png = lowered.endsWith(".png");

        RenderContext ctx;

        if (png)
        {
            // PNGIMAGE は 30KB 近くあるのでスタックには置けない
            PNG *decoder = new (std::nothrow) PNG();
            if (decoder == nullptr)
            {
                log_e("out of memory for PNG decoder");
                return false;
            }

            bool ok = false;
            int opened = decoder->open(path.c_str(), onPngOpen, onPngClose, onPngRead, onPngSeek, onPngDraw);
            if (opened == PNG_SUCCESS)
            {
                int divisor = 1;
                ctx.png = decoder;
                ctx.sourceWidth = decoder->getWidth();
                ctx.sourceHeight = decoder->getHeight();

                // PNG は途中の縮小ができないので、原寸のまま 1 行ずつ間引く
                if (prepare(ctx, ctx.sourceWidth, ctx.sourceHeight, divisor))
                {
                    ctx.line = static_cast<uint16_t *>(ps_malloc(sizeof(uint16_t) * ctx.sourceWidth));
                    if (ctx.line != nullptr)
                    {
                        int decoded = decoder->decode(&ctx, 0);
                        ok = (decoded == PNG_SUCCESS);
                        if (!ok)
                        {
                            log_e("PNG decode failed: %s (error %d, %dx%d)",
                                  path.c_str(), decoded, ctx.sourceWidth, ctx.sourceHeight);
                        }
                        free(ctx.line);
                        ctx.line = nullptr;
                    }
                    else
                    {
                        log_e("out of memory for a PNG line buffer");
                    }
                }
                decoder->close();
            }
            else
            {
                // PNG_TOO_BIG (7) は 1 行がライブラリのバッファに収まらないとき。
                // platformio.ini の PNG_MAX_BUFFERED_PIXELS を上げる。
                log_e("cannot open PNG: %s (error %d)", path.c_str(), opened);
            }

            delete decoder;
            return ok;
        }

        JPEGDEC *decoder = new (std::nothrow) JPEGDEC();
        if (decoder == nullptr)
        {
            log_e("out of memory for JPEG decoder");
            return false;
        }

        File file = Storage::fs().open(path.c_str(), FILE_READ);
        if (!file)
        {
            delete decoder;
            log_w("cannot open JPEG: %s", path.c_str());
            return false;
        }

        bool ok = false;
        if (decoder->open(&file, file.size(), onJpegClose, onJpegRead, onJpegSeek, onJpegDraw) == 1)
        {
            int divisor = 1;
            if (prepare(ctx, decoder->getWidth(), decoder->getHeight(), divisor))
            {
                ok = decodeJpeg(*decoder, ctx, divisor);
                if (!ok)
                {
                    log_e("JPEG decode failed: %s (error %d)", path.c_str(), decoder->getLastError());
                }
            }
            decoder->close();
        }
        else
        {
            log_e("cannot open JPEG: %s (error %d)", path.c_str(), decoder->getLastError());
        }

        file.close();
        delete decoder;
        return ok;
    }

    bool drawMemory(const uint8_t *data, size_t size)
    {
        if (data == nullptr || size == 0)
        {
            return false;
        }

        Screen::setRotation(rotationForHeader(data, size));
        Screen::clear(Screen::kWhite);

        RenderContext ctx;

        if (isPng(data, size))
        {
            PNG *decoder = new (std::nothrow) PNG();
            if (decoder == nullptr)
            {
                log_e("out of memory for PNG decoder");
                return false;
            }

            bool ok = false;
            int opened = decoder->openRAM(const_cast<uint8_t *>(data), static_cast<int>(size), onPngDraw);
            if (opened != PNG_SUCCESS)
            {
                log_e("cannot open PNG from memory (error %d)", opened);
            }
            else
            {
                int divisor = 1;
                ctx.png = decoder;
                ctx.sourceWidth = decoder->getWidth();
                ctx.sourceHeight = decoder->getHeight();

                if (prepare(ctx, ctx.sourceWidth, ctx.sourceHeight, divisor))
                {
                    ctx.line = static_cast<uint16_t *>(ps_malloc(sizeof(uint16_t) * ctx.sourceWidth));
                    if (ctx.line != nullptr)
                    {
                        int decoded = decoder->decode(&ctx, 0);
                        ok = (decoded == PNG_SUCCESS);
                        if (!ok)
                        {
                            log_e("PNG decode failed from memory (error %d)", decoded);
                        }
                        free(ctx.line);
                        ctx.line = nullptr;
                    }
                }
                decoder->close();
            }

            delete decoder;
            return ok;
        }

        JPEGDEC *decoder = new (std::nothrow) JPEGDEC();
        if (decoder == nullptr)
        {
            log_e("out of memory for JPEG decoder");
            return false;
        }

        bool ok = false;
        if (decoder->openRAM(const_cast<uint8_t *>(data), static_cast<int>(size), onJpegDraw) == 1)
        {
            int divisor = 1;
            if (prepare(ctx, decoder->getWidth(), decoder->getHeight(), divisor))
            {
                ok = decodeJpeg(*decoder, ctx, divisor);
            }
            if (!ok)
            {
                log_e("JPEG decode failed from memory (error %d)", decoder->getLastError());
            }
            decoder->close();
        }
        else
        {
            log_e("cannot open JPEG from memory (error %d)", decoder->getLastError());
        }

        delete decoder;
        return ok;
    }
}

#endif
