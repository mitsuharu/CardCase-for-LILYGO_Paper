#include "WebTransfer.h"

#ifdef ARDUINO

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <qrcode.h>

#include <Board.h>
#include <Screen.h>
#include <Input.h>
#include <Storage.h>
#include <ImageFile.h>

namespace
{
    const byte kDnsPort = 53;
    const IPAddress kApAddress(192, 168, 4, 1);
    const IPAddress kApNetmask(255, 255, 255, 0);

    // 受け取った画像を置く名前。上書きしていくので SD が埋まらない。
    // 一覧では [WiFi] から受け取ったものと分かるようにこの名前にしている
    const char *kReceivedPrefix = "/WiFi";

    // 送られてくる形式は PNG と JPEG のどちらもありうる。
    // 拡張子が変わると別の名前になって両方残るので、まとめて消す。
    const char *kReceivedExtensions[] = {".png", ".jpg", ".jpeg"};

    // SD へ書き込みながら受けるときの上限。メモリを使わないので余裕を持たせる。
    const size_t kStreamingCapacity = 4u * 1024u * 1024u;

    // SD が無いときは画像をメモリに持つ。取れるところまで小さくしながら試す。
    const size_t kBufferCandidates[] = {
        4u * 1024u * 1024u,
        2u * 1024u * 1024u,
        1u * 1024u * 1024u,
        512u * 1024u,
    };

    // QR は接続情報（40 文字強）が収まるバージョンにする。
    // 大きくしすぎるとモジュールが細かくなり、電子ペーパーでは読み取りにくい。
    const uint8_t kQrVersion = 6;
    const int kQrScale = 8;
    const int kQrX = 70;
    const int kQrY = 120;

    // 右側に並べる説明の位置
    const int kInfoX = 480;

    WebServer *server = nullptr;
    DNSServer *dns = nullptr;

    // 受け取ったあと、返事がスマホに届くまでの猶予。
    // すぐ WiFi を落とすと接続が切れ、ブラウザが「送信中」のまま止まる。
    const unsigned long kResponseGraceMs = 1500;

    String ssid;
    String password;
    String receivedPath;
    bool received = false;
    unsigned long receivedAt = 0;

    uint8_t *buffer = nullptr;
    size_t bufferSize = 0;
    size_t receivedSize = 0;
    bool uploadFailed = false;

    // SD があるときは書き込みながら受ける。メモリに溜めなくて済む。
    File uploadFile;
    bool usingFile = false;

    void releaseBuffer()
    {
        if (buffer != nullptr)
        {
            free(buffer);
            buffer = nullptr;
        }
        bufferSize = 0;
    }

    // 送られてきた画像を書き込むパスを決める。
    // 拡張子だけ引き継いで、名前は固定にする。
    String pathForUpload(const String &filename)
    {
        String path = String(kReceivedPrefix);
        if (ImageFile::isSupportedImage(filename))
        {
            int dot = filename.lastIndexOf('.');
            String extension = filename.substring(dot);
            extension.toLowerCase();
            path += extension;
        }
        else
        {
            path += ".png";
        }
        return path;
    }

    /// 前に受け取った画像を消す。拡張子が変わっても残らないようにする。
    void removeReceived()
    {
        if (!Storage::isAvailable())
        {
            return;
        }
        for (size_t i = 0; i < sizeof(kReceivedExtensions) / sizeof(kReceivedExtensions[0]); i++)
        {
            String path = String(kReceivedPrefix) + kReceivedExtensions[i];
            if (Storage::fs().exists(path))
            {
                Storage::fs().remove(path);
            }
        }
    }

    /// 確保できるだけのバッファを取る
    bool allocateBuffer()
    {
        releaseBuffer();
        for (size_t i = 0; i < sizeof(kBufferCandidates) / sizeof(kBufferCandidates[0]); i++)
        {
            buffer = static_cast<uint8_t *>(ps_malloc(kBufferCandidates[i]));
            if (buffer != nullptr)
            {
                bufferSize = kBufferCandidates[i];
                log_i("upload: buffer %u KB", (unsigned)(bufferSize / 1024));
                return true;
            }
        }
        return false;
    }

    void handleUpload()
    {
        HTTPUpload &upload = server->upload();

        switch (upload.status)
        {
        case UPLOAD_FILE_START:
            uploadFailed = false;
            receivedSize = 0;
            releaseBuffer();
            receivedPath = "";

            // SD があれば書き込みながら受ける。無ければメモリに溜める。
            usingFile = Storage::isAvailable();
            if (usingFile)
            {
                String path = pathForUpload(upload.filename);
                removeReceived();
                uploadFile = Storage::fs().open(path, FILE_WRITE);
                if (!uploadFile)
                {
                    uploadFailed = true;
                    log_e("upload: cannot open %s", path.c_str());
                    break;
                }
                receivedPath = path;
            }
            else if (!allocateBuffer())
            {
                uploadFailed = true;
                log_e("upload: out of memory");
            }
            break;

        case UPLOAD_FILE_WRITE:
            if (uploadFailed)
            {
                break;
            }
            if (usingFile)
            {
                if (!uploadFile || uploadFile.write(upload.buf, upload.currentSize) != upload.currentSize)
                {
                    uploadFailed = true;
                    log_e("upload: write failed");
                }
                break;
            }
            if (buffer == nullptr || receivedSize + upload.currentSize > bufferSize)
            {
                uploadFailed = true;
                log_e("upload: too large");
                break;
            }
            memcpy(buffer + receivedSize, upload.buf, upload.currentSize);
            receivedSize += upload.currentSize;
            break;

        case UPLOAD_FILE_END:
            if (uploadFile)
            {
                uploadFile.close();
            }
            if (uploadFailed || upload.totalSize == 0)
            {
                releaseBuffer();
                uploadFailed = true;
                break;
            }
            received = true;
            receivedAt = millis();
            log_i("upload: received %u bytes (%s)",
                  (unsigned)upload.totalSize, usingFile ? "sd" : "memory");
            break;

        default:
            if (uploadFile)
            {
                uploadFile.close();
            }
            releaseBuffer();
            uploadFailed = true;
            break;
        }
    }

    // アップロード画面。縮小と向きの補正はここ（ブラウザ側）で行う。
    // 本体は保存するだけにして、減色はパネルへ描くときに任せる。
    // %W% と %H% は配信時に画面の大きさへ差し替える。
    const char kPage[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="ja"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CardCase</title>
<style>
body{font-family:-apple-system,BlinkMacSystemFont,"Hiragino Sans",sans-serif;line-height:1.7;
margin:0 auto;padding:1.5rem 1.25rem 3rem;max-width:32rem;color:#222;background:#fff}
h1{font-size:1.3rem;margin:0 0 .25rem}
p.lead{color:#666;margin:0 0 1.5rem;font-size:.9rem}
label.pick{display:block;text-align:center;padding:1.5rem;border:2px dashed #bbb;border-radius:10px;
cursor:pointer;color:#1257a0;font-weight:600}
.note{margin-top:1rem;padding:.75rem 1rem;background:#fff8e1;border-left:4px solid #d0a000;
border-radius:0 4px 4px 0;font-size:.85rem;color:#555;line-height:1.6}
.note code{background:#fff;padding:.1rem .3rem;border-radius:3px}
.note a{color:#1257a0}
button.copy{width:auto;margin-top:.6rem;padding:.4rem .8rem;font-size:.85rem;
background:#fff;color:#1257a0;border:1px solid #1257a0}
input[type=file]{display:none}
canvas{max-width:100%;margin-top:1rem;border:1px solid #ddd;border-radius:6px;display:none}
button{width:100%;margin-top:1rem;padding:.9rem;font-size:1rem;font-weight:600;color:#fff;
background:#1257a0;border:0;border-radius:8px;cursor:pointer}
button:disabled{background:#9bb4cc}
#status{margin-top:1rem;text-align:center;font-weight:600;min-height:1.5rem}
.ok{color:#1a7f37}.ng{color:#b00}
</style></head><body>
<h1>画像を送る</h1>
<p class="lead">選んだ画像を電子ペーパーに表示します。</p>
<label class="pick" for="file">画像を選ぶ</label>
<input type="file" id="file" accept="image/*">
<p class="note" id="note"></p>
<canvas id="preview"></canvas>
<button id="send" disabled>送信</button>
<div id="status"></div>
<script>
const W = %W%, H = %H%;
const MAXBYTES = %MAXBYTES%;
const file = document.getElementById('file');
const preview = document.getElementById('preview');
const send = document.getElementById('send');
const status = document.getElementById('status');
let blob = null;
let name = 'image.png';

function show(text, cls){ status.textContent = text; status.className = cls || ''; }

const URL_TEXT = 'http://192.168.4.1';

// このアクセスポイントはインターネットに出られないので、
// 端末が携帯回線を優先して本体に届かないことがある。
const MOBILE_HINT = '<br>繋がりにくいときは、モバイルネットワークをオフにしてみてください。';

// クリップボードの API は https でないと使えないので、古いやり方を用意しておく
function copyUrl() {
  const button = document.getElementById('copy');
  const area = document.createElement('textarea');
  area.value = URL_TEXT;
  area.style.position = 'fixed';
  area.style.opacity = '0';
  document.body.appendChild(area);
  area.select();
  let ok = false;
  try { ok = document.execCommand('copy'); } catch (e) { ok = false; }
  document.body.removeChild(area);
  button.textContent = ok ? 'コピーしました' : 'コピーできませんでした';
}

// この画面は WiFi の接続用に開かれた簡易ブラウザで、できることが限られる。
// 制約は OS で違うので、環境に応じて案内を出し分ける。
(function () {
  const note = document.getElementById('note');
  const ua = navigator.userAgent;
  if (/Android/i.test(ua)) {
    // Android の接続画面はファイル選択に対応しておらず、押しても何も起きない。
    //
    // この画面から外部のブラウザは開けない。専用アプリの WebView なので、
    // リンクを押しても同じ画面の中で開くだけになる。intent:// で外に投げる
    // 手もあるが、扱えない場合はエラー画面になって案内ごと消えてしまう。
    // 代わりに URL をコピーできるようにして、貼り付けてもらう。
    note.innerHTML = 'この画面では<strong>画像を選べません</strong>。'
      + 'WiFi の接続画面として開かれているためです。<br>'
      + 'この画面を閉じて、ブラウザで <a href="' + URL_TEXT + '">' + URL_TEXT + '</a> を開いてください。'
      + 'WiFi は繋いだままにしておいてください。'
      + MOBILE_HINT
      + '<br><button type="button" class="copy" id="copy">URL をコピー</button>';
    document.getElementById('copy').addEventListener('click', copyUrl);
  } else if (/iPhone|iPad|iPod/i.test(ua)) {
    // iOS の接続画面はカメラを起動できず、選ぶとシートごと閉じてしまう
    note.innerHTML = 'この画面で<strong>カメラは使えません</strong>。'
      + 'WiFi の接続画面として開かれているためです。<br>'
      + '<strong>写真ライブラリから選んでください。</strong>'
      + '撮った写真を送るときは、先にカメラアプリで撮影しておいてください。'
      + '<br>ブラウザで <a href="' + URL_TEXT + '">' + URL_TEXT + '</a> を開いても使えます。'
      + MOBILE_HINT
      + '<br><button type="button" class="copy" id="copy">URL をコピー</button>';
    document.getElementById('copy').addEventListener('click', copyUrl);
  } else {
    note.style.display = 'none';
  }
})();
function toBlob(type, q){ return new Promise(r => preview.toBlob(r, type, q)); }

// カメラで撮った写真は 1200 万画素になることもある。
// ImageBitmap に展開するとスマホのメモリを使い切って落ちるので、
// img 要素に読ませてブラウザに任せる。EXIF の向きもここで反映される。
function load(f) {
  return new Promise((resolve, reject) => {
    const url = URL.createObjectURL(f);
    const img = new Image();
    img.onload = () => { URL.revokeObjectURL(url); resolve(img); };
    img.onerror = () => { URL.revokeObjectURL(url); reject(); };
    img.src = url;
  });
}

// 指定の倍率で描き直す
function draw(img, scale) {
  preview.width = Math.max(1, Math.round(img.naturalWidth * scale));
  preview.height = Math.max(1, Math.round(img.naturalHeight * scale));
  const ctx = preview.getContext('2d');
  ctx.fillStyle = '#fff';
  ctx.fillRect(0, 0, preview.width, preview.height);
  ctx.drawImage(img, 0, 0, preview.width, preview.height);
}

// 本体が受け取れる大きさに収まるまで、圧縮を強めながら小さくしていく。
// 試す回数が多いとスマホ側の負荷が大きいので、段階は絞ってある。
async function encode(img, baseScale) {
  for (let step = 0; step < 3; step++) {
    draw(img, baseScale * Math.pow(0.7, step));

    // 図や文字の画像は PNG のまま送りたいので、最初だけ PNG を試す
    if (step === 0) {
      const png = await toBlob('image/png');
      if (png && png.size <= MAXBYTES) { return { data: png, ext: 'png' }; }
    }
    for (const q of [0.9, 0.75, 0.6]) {
      const jpeg = await toBlob('image/jpeg', q);
      if (jpeg && jpeg.size <= MAXBYTES) { return { data: jpeg, ext: 'jpg' }; }
    }
  }
  return null;
}

file.addEventListener('change', async () => {
  const f = file.files[0];
  if (!f) return;
  show('読み込み中...');
  send.disabled = true;
  blob = null;
  try {
    const img = await load(f);
    // 本体は画像の向きに合わせて画面を回すので、実際に表示される枠は
    // 画像が横長か縦長かで変わる。その枠に収まる大きさまで縮める。
    // 比率は変えない。向きの調整は本体側に任せる。
    const sameOrientation = (img.naturalWidth > img.naturalHeight) === (W > H);
    const boxW = sameOrientation ? W : H;
    const boxH = sameOrientation ? H : W;
    const scale = Math.min(1, boxW / img.naturalWidth, boxH / img.naturalHeight);

    const encoded = await encode(img, scale);
    preview.style.display = 'block';

    if (!encoded) {
      show('この画像は大きすぎて送れません', 'ng');
      return;
    }
    blob = encoded.data;
    name = 'image.' + encoded.ext;
    send.disabled = false;
    show(preview.width + ' x ' + preview.height + ' / ' + Math.round(blob.size / 1024) + ' KB');
  } catch (e) {
    show('この画像は読み込めませんでした', 'ng');
  }
});

send.addEventListener('click', () => {
  if (!blob) return;
  send.disabled = true;
  show('送信中...');
  const body = new FormData();
  body.append('image', blob, name);
  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/upload');
  xhr.upload.onprogress = e => {
    if (e.lengthComputable) show('送信中... ' + Math.round(e.loaded / e.total * 100) + '%');
  };
  xhr.onload = () => {
    if (xhr.status === 200) { show('送信しました。本体に表示されます。', 'ok'); }
    else { show('送信に失敗しました', 'ng'); send.disabled = false; }
  };
  xhr.onerror = () => { show('送信に失敗しました', 'ng'); send.disabled = false; };
  xhr.send(body);
});
</script></body></html>)HTML";

    void handlePage()
    {
        // 画面より大きい画像を受け取っても表示には使えないので、
        // 送る前にブラウザ側で縮めてもらう。SD の消費も減る。
        //
        // あわせて受け入れられるバイト数も伝える。SD へ書き込みながら受ける場合は
        // 余裕があるが、メモリに溜める場合は確保できた分しか受け取れない。
        size_t capacity = Storage::isAvailable() ? kStreamingCapacity : bufferSize;

        String html = String(FPSTR(kPage));
        html.replace("%W%", String(Layout::kPanelWidth));
        html.replace("%H%", String(Layout::kPanelHeight));
        html.replace("%MAXBYTES%", String((uint32_t)capacity));
        server->send(200, "text/html; charset=utf-8", html);
    }

    void registerRoutes(WebServer &target)
    {
        target.on("/", HTTP_GET, handlePage);
        target.on("/upload", HTTP_POST,
                  []() { server->send(uploadFailed ? 500 : 200, "text/plain", uploadFailed ? "NG" : "OK"); },
                  handleUpload);

        // 繋いだ時点でスマホが接続確認に来るので、そこにこの画面を返す。
        // すると自動でアップロード画面が開く。
        target.onNotFound(handlePage);
    }

    /// QR コードをフレームバッファへ描く
    void drawQrCode(const String &payload)
    {
        QRCode qrcode;
        uint8_t *data = static_cast<uint8_t *>(malloc(qrcode_getBufferSize(kQrVersion)));
        if (data == nullptr)
        {
            return;
        }

        if (qrcode_initText(&qrcode, data, kQrVersion, ECC_LOW, payload.c_str()) != 0)
        {
            free(data);
            log_e("failed to build a QR code");
            return;
        }

        int side = qrcode.size * kQrScale;

        // 余白（クワイエットゾーン）が無いと読み取れない。地の色で 4 モジュールぶん囲む。
        int quiet = kQrScale * 4;
        Screen::fillRect(kQrX - quiet, kQrY - quiet, side + quiet * 2, side + quiet * 2, Screen::kWhite);

        for (uint8_t y = 0; y < qrcode.size; y++)
        {
            for (uint8_t x = 0; x < qrcode.size; x++)
            {
                if (!qrcode_getModule(&qrcode, x, y))
                {
                    continue;
                }
                Screen::fillRect(kQrX + x * kQrScale, kQrY + y * kQrScale,
                                 kQrScale, kQrScale, Screen::kBlack);
            }
        }

        free(data);
    }
}

namespace WebTransfer
{
    bool begin()
    {
        received = false;
        receivedAt = 0;
        receivedPath = "";
        releaseBuffer();

        // SD があれば書き込みながら受けられるので、メモリは要らない。
        // 無い場合はここで確保しておく。受け取っている途中で失敗させないためと、
        // 実際に確保できた量をブラウザに伝えて、その中に収めてもらうため。
        if (!Storage::isAvailable() && !allocateBuffer())
        {
            log_e("no memory for receiving");
            return false;
        }

        uint8_t mac[6] = {0};
        WiFi.macAddress(mac);
        ssid = Credentials::ssidFor(mac);
        password = Credentials::passwordFor(mac);

        WiFi.mode(WIFI_AP);
        WiFi.softAPConfig(kApAddress, kApAddress, kApNetmask);
        if (!WiFi.softAP(ssid.c_str(), password.c_str()))
        {
            log_e("failed to start softAP");
            return false;
        }

        // すべての問い合わせを自分に向ける。
        // スマホが接続確認に行った先がこの画面になるので、自動で開く。
        dns = new DNSServer();
        dns->setErrorReplyCode(DNSReplyCode::NoError);
        dns->start(kDnsPort, "*", kApAddress);

        server = new WebServer(80);
        registerRoutes(*server);
        server->begin();

        log_i("softAP started: %s", ssid.c_str());
        return true;
    }

    void update()
    {
        if (dns != nullptr)
        {
            dns->processNextRequest();
        }
        if (server != nullptr)
        {
            server->handleClient();
        }
    }

    void end()
    {
        if (server != nullptr)
        {
            server->stop();
            delete server;
            server = nullptr;
        }
        if (dns != nullptr)
        {
            dns->stop();
            delete dns;
            dns = nullptr;
        }

        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);
    }

    void render()
    {
        drawQrCode(Credentials::wifiQrPayload(ssid, password));

        const GFXfont *title = Screen::Fonts::row();
        const GFXfont *note = Screen::Fonts::small();

        int y = kQrY + title->ascender;
        Screen::drawText(title, "Scan to connect", kInfoX, y);

        y += title->advance_y * 2;
        Screen::drawText(title, (String("SSID: ") + ssid).c_str(), kInfoX, y);

        y += title->advance_y;
        Screen::drawText(title, (String("PASS: ") + password).c_str(), kInfoX, y);

        y += title->advance_y * 2;
        Screen::drawText(note, "Open this if the page does not appear:", kInfoX, y);

        y += note->advance_y + title->ascender;
        Screen::drawText(title, "http://192.168.4.1", kInfoX, y);

        // 戻り方は機種で変わる。タッチが無い版では画面を叩いても何も起きない。
        const char *guide = Input::hasTouch()
                                ? "Tap the screen, or hold BOOT, to go back to the list."
                                : "BOOT: hold = back to the list";
        Screen::drawText(note, guide, Layout::kMargin, Layout::kFooterLine2Baseline);
    }

    bool hasReceivedImage()
    {
        // 返事を返しきるまでは知らせない。
        // 呼び出し側はその間も update() を回し続けるので、送信が完了する。
        if (!received)
        {
            return false;
        }
        return static_cast<long>(millis() - (receivedAt + kResponseGraceMs)) >= 0;
    }

    const uint8_t *receivedImage(size_t &size)
    {
        size = receivedSize;
        return buffer;
    }

    String receivedImagePath()
    {
        return receivedPath;
    }

    void releaseReceivedImage()
    {
        releaseBuffer();
        received = false;
        receivedAt = 0;
        receivedSize = 0;
    }
}

#endif
