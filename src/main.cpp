#include <Arduino.h>

#include <Board.h>
#include <Screen.h>
#include <Input.h>
#include <Storage.h>
#include <ImageFile.h>
#include <ImageOrder.h>
#include <ImageDraw.h>
#include <Menu.h>
#include <WebTransfer.h>

/// 画像を表示したあと、操作で一覧に戻れる時間。これを過ぎたら電池のため寝る。
constexpr unsigned long kViewingTimeoutMs = 60000;

/// いま画面に出しているもの
enum class Mode
{
    Browsing,     // 一覧
    Viewing,      // 画像
    Transferring, // WiFi で画像を待っている
};

Menu menu;
Mode mode = Mode::Browsing;
unsigned long viewingUntil = 0;

// 画像の名前を名前順に並べておく場所。
// 一覧の先頭は [WiFi] が占めるので、画像に使えるのはそのぶん少ない。
constexpr int kMaxImages = Menu::kMaxItems - 1;
String imageNames[kMaxImages];

// 画像を受け取ると SD の中身が変わるので、一覧を作り直す必要がある
bool imageListStale = false;

void buildMenu();
void onSelectItem(const MenuItem &item);

/// 後始末をしてからディープスリープに入る
void enterDeepSleep()
{
    Storage::end();
    Input::end();
    Screen::powerOff();

    // タッチの割り込み線は RTC-IO ではないので、復帰は IO21 のボタンだけ。
    esp_sleep_enable_ext1_wakeup(1ULL << Board::kButton, ESP_EXT1_WAKEUP_ANY_LOW);

    log_i("deep sleep start");
    esp_deep_sleep_start();
}

/// 見出しを描く。転送は呼び出し側がまとめて行う。
void drawHeader()
{
    const GFXfont *font = Screen::Fonts::header();
    Screen::drawText(font, "CardCase for T5", Layout::kMargin, Layout::kHeaderBaseline);

    // SD が無いと一覧が [WiFi] だけになる。理由が分からないと故障に見えるので出す。
    if (!Storage::isAvailable())
    {
        const GFXfont *note = Screen::Fonts::small();
        const char *message = "no SD card";
        int x = Layout::kPanelWidth - Layout::kMargin - Screen::textWidth(note, message);
        Screen::drawText(note, message, x, Layout::kHeaderBaseline);
    }

    Screen::drawHLine(Layout::kMargin, Layout::kRuleY,
                      Layout::kPanelWidth - Layout::kMargin * 2, Screen::kBlack);
}

/// 致命的なエラーを表示して停止する
void halt(const char *message)
{
    Screen::setRotation(0);
    Screen::clear();
    drawHeader();
    Screen::drawText(Screen::Fonts::row(), message,
                     Layout::kMargin, Layout::kListTop + Screen::Fonts::row()->ascender);
    Screen::flush(true);

    while (true)
    {
        delay(1000);
    }
}

/// 画像を全画面表示して、戻れる状態にする
void showImage(const String &path)
{
    if (!ImageDraw::drawFile(path))
    {
        // 壊れたファイルや未対応の形式。何も出ないと固まったように見える。
        Screen::setRotation(0);
        Screen::clear();
        drawHeader();
        Screen::drawText(Screen::Fonts::row(), "cannot show this image.",
                         Layout::kMargin, Layout::kListTop + Screen::Fonts::row()->ascender);
    }

    Screen::flush(true);
    Input::pauseTouch();
    Input::discardPending();

    mode = Mode::Viewing;
    viewingUntil = millis() + kViewingTimeoutMs;
}

/// メモリ上の画像を表示する（SD が無いときに WiFi で受け取ったもの）
void showImageFromMemory(const uint8_t *data, size_t size)
{
    if (!ImageDraw::drawMemory(data, size))
    {
        Screen::setRotation(0);
        Screen::clear();
        drawHeader();
        Screen::drawText(Screen::Fonts::row(), "cannot show this image.",
                         Layout::kMargin, Layout::kListTop + Screen::Fonts::row()->ascender);
    }

    Screen::flush(true);
    Input::pauseTouch();
    Input::discardPending();

    mode = Mode::Viewing;
    viewingUntil = millis() + kViewingTimeoutMs;
}

/// 一覧へ戻る
void returnToMenu()
{
    // 画像は EXIF や画面合わせで回してあるので、一覧の向きに戻す
    Screen::setRotation(0);
    Screen::clear();
    drawHeader();

    if (imageListStale)
    {
        // 受け取った画像が増えているので作り直す
        buildMenu();
        menu.begin(onSelectItem);
        imageListStale = false;
    }
    else
    {
        // 選んでいた位置を保ちたいので begin() ではなく draw() を呼ぶ
        menu.draw();
    }

    // 直前まで画像や QR を出していて画面全体が変わるので、残像を消し切る
    Screen::flush(true);
    Input::pauseTouch();
    Input::discardPending();

    mode = Mode::Browsing;
}

/// WiFi で画像を受け取る状態に入る
void startTransfer()
{
    if (!WebTransfer::begin())
    {
        halt("failed to start WiFi.");
    }

    Screen::setRotation(0);
    Screen::clear();
    drawHeader();
    WebTransfer::render();

    // QR は細かい模様なので、読み取れるようにしっかり出す
    Screen::flush(true);
    Input::pauseTouch();
    Input::discardPending();

    mode = Mode::Transferring;
}

/// 一覧で選ばれたときの処理
void onSelectItem(const MenuItem &item)
{
    if (item.kind == MenuItemKind::Transfer)
    {
        startTransfer();
        return;
    }
    showImage(item.value);
}

/**
 * SD を走査して画像ファイルを一覧に積む。
 *
 * `openNextFile()` は FAT に登録された順（おおむね書き込んだ順）で返してくるので、
 * そのまま積むとカードに入れた順に並ぶ。名前順へ入れ替えてから積む。
 *
 * 名前は SortedNames が順番を保って持つ。上限を超えたぶんはそこで捨てられるので、
 * 全部を読んでから並べ替えて切るのに比べて、覚えておく数が一覧に載る数を超えない。
 */
void collectImages()
{
    File root = Storage::fs().open("/");
    if (!root)
    {
        return;
    }

    ImageFile::SortedNames names(imageNames, kMaxImages);

    File file = root.openNextFile();
    while (file)
    {
        if (!file.isDirectory())
        {
            String filename = file.name();
            if (ImageFile::isListable(filename))
            {
                names.insert(filename);
            }
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();

    for (int i = 0; i < names.count(); i++)
    {
        const String &filename = names.at(i);
        menu.addItem(MenuItemKind::Image, filename, ImageFile::rootPath(filename));
    }
}

/**
 * 一覧を作り直す。
 *
 * 画像を受け取ると SD の中身が変わるので、起動時に作ったままだと
 * 増えた画像が出てこない。
 */
void buildMenu()
{
    menu.clear();

    // 受け取る導線は末尾ではなく先頭に置く。
    // 画像が増えても位置が変わらず、探さずに選べる。
    // ボタンだけで操作する機種では 1 手で届くのが効く。
    menu.addItem(MenuItemKind::Transfer, "[WiFi]  receive an image from your phone", "");

    if (Storage::isAvailable())
    {
        collectImages();
    }
}

void setup()
{
    Serial.begin(115200);

    if (!Screen::begin())
    {
        // PSRAM が無効だとフレームバッファを確保できない。
        // 画面に出す手段そのものが無いので、ログだけ残して止める。
        log_e("failed to allocate the framebuffer. is PSRAM enabled?");
        while (true)
        {
            delay(1000);
        }
    }

    // タッチが無くてもボタンで操作できるので、見つからなくても止めない
    Input::begin();

    // SD が無くても WiFi で画像を受け取って表示はできるので、ここでは止めない
    if (!Storage::begin())
    {
        log_w("SD card is not available");
    }

    buildMenu();

    // 電子ペーパーの全面更新は 600ms 前後かかるので、見出しから一覧まで
    // フレームバッファに描き切ってから 1 回で転送する
    Screen::clear();
    drawHeader();
    menu.begin(onSelectItem);

    // 起動直後は前回の絵が残っているので、消し切れるように全面で出す
    Screen::flush(true);
    Input::pauseTouch();
}

void loop()
{
    Input::update();

    if (mode == Mode::Transferring)
    {
        WebTransfer::update();

        if (WebTransfer::hasReceivedImage())
        {
            // 受け取ったらすぐ表示する。待つ必要はないので電波は止める。
            WebTransfer::end();

            String path = WebTransfer::receivedImagePath();
            if (path.length() > 0)
            {
                imageListStale = true;
                WebTransfer::releaseReceivedImage();
                showImage(path);
            }
            else
            {
                size_t size = 0;
                const uint8_t *image = WebTransfer::receivedImage(size);
                showImageFromMemory(image, size);
                WebTransfer::releaseReceivedImage();
            }
            return;
        }

        if (Input::wasAnyInput())
        {
            WebTransfer::end();
            returnToMenu();
        }

        delay(5);
        return;
    }

    if (mode == Mode::Viewing)
    {
        // 戻すのはボタンだけにする。名刺として相手に画面を見せている最中に
        // 触れられることがあり、タッチで戻すと見せている画像がその場で消える。
        bool returning = Input::wasButtonInput();

        // 触れられた記録は残さない。次に一覧へ戻ったときの操作として拾われる。
        Input::discardTouch();

        if (returning)
        {
            returnToMenu();
        }
        else if (static_cast<long>(millis() - viewingUntil) >= 0)
        {
            // 電池のためスリープに入る。再び画像を選びたいときは IO21 のボタンを押す。
            enterDeepSleep();
        }

        delay(10);
        return;
    }

    menu.update();
    delay(10);
}
