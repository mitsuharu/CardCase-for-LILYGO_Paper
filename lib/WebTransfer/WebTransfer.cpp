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
    //
    // 文字を送る場合も、ブラウザ側で画像にしてから同じ経路で送る。
    // 本体にフォントを持たせて描くこともできるが、日本語のフォントは
    // フラッシュを大きく食う上に、画面に合わせた組版も要る。
    // スマホのフォントで描いて画像にすれば、どちらも要らない。
    //
    // %W% と %H% は配信時に画面の大きさへ差し替える。
    const char kPage[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="ja"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CardCase</title>
<style>
body{font-family:-apple-system,BlinkMacSystemFont,"Hiragino Sans",sans-serif;line-height:1.7;
margin:0 auto;padding:1.5rem 1.25rem 3rem;max-width:32rem;color:#222;background:#fff}
h1{font-size:1.3rem;margin:0 0 .25rem}
p.lead{color:#666;margin:0 0 1.25rem;font-size:.9rem}
.tabs{display:flex;gap:.5rem;margin-bottom:1rem}
.tabs button{flex:1;margin:0;padding:.6rem;font-size:.95rem;background:#fff;color:#1257a0;
border:1px solid #1257a0;border-radius:8px}
.tabs button.on{background:#1257a0;color:#fff}
label.pick{display:block;text-align:center;padding:1.5rem;border:2px dashed #bbb;border-radius:10px;
cursor:pointer;color:#1257a0;font-weight:600}
textarea{width:100%;box-sizing:border-box;padding:.75rem;font-size:1rem;line-height:1.6;
font-family:inherit;border:1px solid #bbb;border-radius:8px;resize:vertical}
.size{display:flex;align-items:flex-end;gap:.5rem;margin-top:.75rem}
.size label{font-size:.85rem;color:#555}
.size input{width:100%;box-sizing:border-box;padding:.5rem;font-size:1rem;
border:1px solid #bbb;border-radius:6px}
.size button{width:auto;margin:0;padding:.5rem .7rem;font-size:.85rem;white-space:nowrap;
background:#fff;color:#1257a0;border:1px solid #1257a0}
.row{display:flex;align-items:center;gap:.6rem;margin-top:.75rem}
.row .caption{font-size:.85rem;color:#555;white-space:nowrap}
.seg{display:flex;gap:.35rem}
.seg button{width:auto;margin:0;padding:.4rem .7rem;font-size:.85rem;background:#fff;
color:#1257a0;border:1px solid #1257a0;border-radius:6px}
.seg button.on{background:#1257a0;color:#fff}
input[type=range]{flex:1;min-width:0}
.row .value{font-size:.8rem;color:#777;white-space:nowrap;min-width:6rem;text-align:right}
.line{margin-top:.75rem}
.line label{display:block;font-size:.85rem;color:#555}
.line input{width:100%;box-sizing:border-box;margin-top:.2rem;padding:.5rem;font-size:1rem;
font-family:inherit;border:1px solid #bbb;border-radius:6px}
.hint{margin:.5rem 0 0;font-size:.8rem;color:#777}
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
<h1>電子ペーパーに送る</h1>
<p class="lead">選んだ画像、書いた文字、または名刺を表示します。</p>
<div class="tabs">
<button type="button" id="tab-image" class="on">画像</button>
<button type="button" id="tab-text">テキスト</button>
<button type="button" id="tab-card">名刺</button>
</div>
<div id="pane-image">
<label class="pick" for="file">画像を選ぶ</label>
<input type="file" id="file" accept="image/*">
<p class="note" id="note"></p>
</div>
<div id="pane-text" hidden>
<textarea id="text" rows="4" placeholder="表示する文字&#10;改行するとそのまま改行されます"></textarea>
<div class="size">
<label>幅<input type="number" id="tw" min="16" max="2000" step="1" value="%W%"></label>
<label>高さ<input type="number" id="th" min="16" max="2000" step="1" value="%H%"></label>
<button type="button" id="swap">縦横を入れ替え</button>
</div>
<div class="row">
<span class="caption">折り返し</span>
<div class="seg" id="reflow">
<button type="button" data-reflow="auto" class="on">自動</button>
<button type="button" data-reflow="keep">しない</button>
</div>
</div>
<div class="row">
<span class="caption">文字寄せ</span>
<div class="seg" id="align">
<button type="button" data-align="left">左</button>
<button type="button" data-align="center" class="on">中央</button>
<button type="button" data-align="right">右</button>
</div>
</div>
<div class="row">
<span class="caption">文字の大きさ</span>
<input type="range" id="ratio" min="40" max="100" step="5" value="100">
<span class="value" id="ratio-value">自動</span>
</div>
<p class="hint">本体の画面は %W% x %H% です。同じ大きさにすると、拡大されずいちばんきれいに出ます。</p>
</div>
<div id="pane-card" hidden>
<label class="pick" for="cardfile">画像を選ぶ（任意）</label>
<input type="file" id="cardfile" accept="image/*">
<button type="button" class="copy" id="card-clear" hidden>画像を外す</button>
<p class="note" id="card-note"></p>
<div class="line"><label>タイトル<input type="text" id="card-title" placeholder="山田 太郎"></label></div>
<div class="line"><label>サブタイトル<input type="text" id="card-subtitle" placeholder="Taro Yamada"></label></div>
<div class="line"><label>SNS アカウント<input type="text" id="card-account" placeholder="@example"></label></div>
<div class="line"><label>QR にする URL<input type="url" id="card-url" inputmode="url" placeholder="https://example.com/"></label></div>
<div class="size">
<label>幅<input type="number" id="card-w" min="16" max="2000" step="1" value="%W%"></label>
<label>高さ<input type="number" id="card-h" min="16" max="2000" step="1" value="%H%"></label>
<button type="button" id="card-swap">縦横を入れ替え</button>
</div>
<div class="row">
<span class="caption">文字の大きさ</span>
<input type="range" id="card-ratio" min="40" max="100" step="5" value="100">
<span class="value" id="card-ratio-value">自動</span>
</div>
<p class="hint">空にした項目は詰めて並べます。横長にすると画像を左、文字と QR を右に置きます。</p>
</div>
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
const text = document.getElementById('text');
const tw = document.getElementById('tw');
const th = document.getElementById('th');
const ratio = document.getElementById('ratio');
const cardfile = document.getElementById('cardfile');
const cardTitle = document.getElementById('card-title');
const cardSubtitle = document.getElementById('card-subtitle');
const cardAccount = document.getElementById('card-account');
const cardUrl = document.getElementById('card-url');
const cardW = document.getElementById('card-w');
const cardH = document.getElementById('card-h');
const cardClear = document.getElementById('card-clear');
const cardRatio = document.getElementById('card-ratio');
// 名刺に載せる画像。選ばなくてもよい。
let cardImage = null;
let alignment = 'center';
// 'auto' は読めなくなるなら折り返す。'keep' は書いた行のままにする。
let reflow = 'auto';
let blob = null;
let name = 'image.png';

// プレビューを描き直す手続き。画像とテキストで中身が変わるだけで、
// 送るまでの流れ（縮めながら収まる大きさを探す）は同じにしてある。
let render = null;

function show(message, cls){ status.textContent = message; status.className = cls || ''; }

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
// どちらの制約もファイル選択に関わるものなので、テキストはこの画面のまま送れる。
(function () {
  const note = document.getElementById('note');
  const cardNote = document.getElementById('card-note');
  const ua = navigator.userAgent;
  const TEXT_HINT = '<br><strong>テキスト</strong>なら、この画面のままでも送れます。';
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
      + TEXT_HINT
      + '<br><button type="button" class="copy" id="copy">URL をコピー</button>';
    document.getElementById('copy').addEventListener('click', copyUrl);
    // 名刺は画像が任意なので、文字と QR だけならこの画面のままで作れる
    cardNote.innerHTML = 'この画面では<strong>画像を選べません</strong>。'
      + '画像を使わない名刺（文字と QR だけ）なら、このまま作れます。';
  } else if (/iPhone|iPad|iPod/i.test(ua)) {
    // iOS の接続画面はカメラを起動できず、選ぶとシートごと閉じてしまう
    note.innerHTML = 'この画面で<strong>カメラは使えません</strong>。'
      + 'WiFi の接続画面として開かれているためです。<br>'
      + '<strong>写真ライブラリから選んでください。</strong>'
      + '撮った写真を送るときは、先にカメラアプリで撮影しておいてください。'
      + '<br>ブラウザで <a href="' + URL_TEXT + '">' + URL_TEXT + '</a> を開いても使えます。'
      + MOBILE_HINT
      + TEXT_HINT
      + '<br><button type="button" class="copy" id="copy">URL をコピー</button>';
    document.getElementById('copy').addEventListener('click', copyUrl);
    cardNote.innerHTML = 'この画面で<strong>カメラは使えません</strong>。'
      + '名刺に載せる画像は、写真ライブラリから選んでください。';
  } else {
    note.style.display = 'none';
    cardNote.style.display = 'none';
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

// 実際に描いた文字の大きさ。画面に出して、目安が分かるようにする。
let drawnSize = 0;

function context(w, h) {
  preview.width = Math.max(1, Math.round(w));
  preview.height = Math.max(1, Math.round(h));
  const ctx = preview.getContext('2d');
  ctx.fillStyle = '#fff';
  ctx.fillRect(0, 0, preview.width, preview.height);
  return ctx;
}

// 指定の倍率で描き直す
function drawImage(img, scale) {
  drawnSize = 0;
  const ctx = context(img.naturalWidth * scale, img.naturalHeight * scale);
  ctx.drawImage(img, 0, 0, preview.width, preview.height);
}

// 電子ペーパーは階調が粗く、これを切ると画数の多い漢字が潰れて読めない。
// 折り返すかどうかと、割合で小さくするときの下限の両方でこの値を使う。
const READABLE = 12;

// 折り返しの単位。日本語は語の切れ目が無いので 1 文字ずつ送るが、
// 英数字とアドレスは途中で切れると読めなくなるので塊のまま扱う。
function tokenize(line) {
  return line.match(/[A-Za-z0-9@._:\/+-]+|[\s\S]/g) || [];
}

// 幅に収まるように折り返す。塊のままでは入らないものは文字単位に割り、
// 割ったことを呼び出し側に伝える。語の途中で改行するくらいなら、
// 文字を小さくして 1 行に収めるほうが読みやすいため。
function wrap(ctx, body, maxWidth) {
  const lines = [];
  let broken = false;
  for (const paragraph of body.split('\n')) {
    let current = '';
    for (const token of tokenize(paragraph)) {
      let parts = [token];
      if (ctx.measureText(token).width > maxWidth) {
        parts = Array.from(token);
        broken = broken || parts.length > 1;
      }
      for (const part of parts) {
        if (current !== '' && ctx.measureText(current + part).width > maxWidth) {
          lines.push(current);
          current = (part === ' ') ? '' : part;
        } else {
          current += part;
        }
      }
    }
    lines.push(current);
  }
  return { lines: lines, broken: broken };
}

function widest(ctx, lines) {
  let max = 0;
  for (const line of lines) {
    max = Math.max(max, ctx.measureText(line).width);
  }
  return max;
}

// 電子ペーパーは階調が粗く細い線が飛ぶので、太字で描く。
function fontOf(size) {
  return 'bold ' + size + 'px "Hiragino Sans","Noto Sans JP",sans-serif';
}

// 枠に収まる最大の文字の大きさを二分探索で決める。
// 1 段ずつ試すと文字数が多いときに時間がかかる。
//
// level は折り返しをどこまで許すか。
//   0 … 書いたとおりの行のまま。折り返さない
//   1 … 折り返してよい。ただし語の途中では切らない
//   2 … 語の途中でも切る
function fit(ctx, body, innerWidth, innerHeight, level) {
  const paragraphs = body.split('\n').length;
  let low = 4;
  let high = innerHeight;
  let best = null;
  while (low <= high) {
    const size = (low + high) >> 1;
    ctx.font = fontOf(size);
    const wrapped = wrap(ctx, body, innerWidth);
    const lineHeight = Math.ceil(size * 1.35);
    const allowed = level >= 2
      || (level === 1 && !wrapped.broken)
      || (level === 0 && wrapped.lines.length === paragraphs);
    const fits = allowed
      && wrapped.lines.length * lineHeight <= innerHeight
      && widest(ctx, wrapped.lines) <= innerWidth;
    if (fits) {
      best = { lines: wrapped.lines, size: size, lineHeight: lineHeight };
      low = size + 1;
    } else {
      high = size - 1;
    }
  }
  return best;
}

// 文字を canvas に描く。実際に使った文字の大きさを返す（描かなければ 0）。
//
// 大きさは枠に収まる最大を探して決める。枠は機種の画面と同じとは限らず、
// 文字数も毎回違うので、固定の値では入り切らないか小さすぎるかのどちらかになる。
// share は枠いっぱい（自動）に対する割合。小さくしたいときだけ 1 未満にする。
// reflow は折り返しの扱い。'keep' なら書いた行のままにする。
function paintText(ctx, width, height, body, share, align, reflow) {
  ctx.fillStyle = '#fff';
  ctx.fillRect(0, 0, width, height);
  if (body.trim() === '') {
    return 0;
  }

  // 余白。ベゼルに隠れる分と、名刺として見たときの見栄えの両方から取る。
  const padding = Math.round(Math.min(width, height) * 0.08);
  const innerWidth = Math.max(1, width - padding * 2);
  const innerHeight = Math.max(1, height - padding * 2);

  // 書いた人が入れた改行を優先する。日本語は語の切れ目が無いので、
  // 折り返しを先に許すと「山田 太」「郎」のように名前が割れたまま、
  // そのぶん字を大きくできてしまう。
  //
  // ただし、そのために読めない大きさになるなら折り返しに任せる。長い 1 行を
  // そのまま入れようとすると極端に小さくなる（540x960 に 60 字を 1 行で入れると
  // 7px、折り返せば 64px）。名刺として使えるかどうかは、そちらで決まる。
  //
  // 折り返しても入らなければ語の途中でも切る。それでも駄目なときは何も描かない。
  // 呼び出し側が「入らない」と知らせる。
  //
  // 'keep' が選ばれているときは、小さくなっても書いた行のままにする。
  // 表の見出しのように、折り返されると意味が変わるものがあるため。
  const written = fit(ctx, body, innerWidth, innerHeight, 0);
  const best = (reflow === 'keep' || (written !== null && written.size >= READABLE))
    ? written
    : (fit(ctx, body, innerWidth, innerHeight, 1)
      || fit(ctx, body, innerWidth, innerHeight, 2));
  if (best === null) {
    return 0;
  }

  // 枠いっぱいを上限に、指定の割合まで小さくする。
  // 小さくすると 1 行に入る文字数が変わるので、折り返しはその大きさで取り直す。
  //
  // 自動でそこまで小さくなる場合（文字数が多いとき）は仕方がないが、
  // 割合の指定で読めない大きさまで落とさない。
  const floor = Math.min(best.size, READABLE);
  const size = Math.max(floor, Math.round(best.size * share));
  ctx.font = fontOf(size);
  const wrapped = wrap(ctx, body, innerWidth);
  const lineHeight = Math.ceil(size * 1.35);

  ctx.fillStyle = '#000';
  ctx.textAlign = align;
  ctx.textBaseline = 'middle';

  // 縦は常に中央に置く。上下の寄せは、電子ペーパーのベゼルに近づくほど
  // 読みにくくなるだけで、名刺の見え方としても得るものが無い。
  let y = (height - wrapped.lines.length * lineHeight) / 2 + lineHeight / 2;
  const x = align === 'left' ? padding : (align === 'right' ? width - padding : width / 2);
  for (const line of wrapped.lines) {
    ctx.fillText(line, x, y);
    y += lineHeight;
  }
  return size;
}

// 名刺の QR。URL を読み取ってもらうためだけに使う。
//
// 本体にもアプリにも QR を作る手段が無く、この画面はインターネットに
// 出られないので、外から持ってくることもできない。ここで作る。
//
// 用途を URL 1 本に絞って、8 ビットモード・誤り訂正 M・型番 1〜10 だけを
// 扱う（213 バイトまで）。名刺に載せる URL には十分で、そのぶん表が短い。
const QUIET = 4;

// 型番ごとの [ブロック 1 つの誤り訂正語数, 群 1 のブロック数, その語数, 群 2 のブロック数, その語数]。
// 誤り訂正は M（15% ほど復元できる）。汚れやすいものではないので、これで足りる。
const QR_BLOCKS = [
  [10, 1, 16, 0, 0],
  [16, 1, 28, 0, 0],
  [26, 1, 44, 0, 0],
  [18, 2, 32, 0, 0],
  [24, 2, 43, 0, 0],
  [16, 4, 27, 0, 0],
  [18, 4, 31, 0, 0],
  [22, 2, 38, 2, 39],
  [22, 3, 36, 2, 37],
  [26, 4, 43, 1, 44],
];

// 型番ごとの位置合わせパターンの中心。角の 3 つはファインダと重なるので置かない。
const QR_ALIGN = [
  [], [6, 18], [6, 22], [6, 26], [6, 30],
  [6, 34], [6, 22, 38], [6, 24, 42], [6, 26, 46], [6, 28, 50],
];

// 誤り訂正の計算に使う体（GF(256)）の対数表。掛け算を足し算にするために持つ。
const QR_EXP = [];
const QR_LOG = [];
(function () {
  let value = 1;
  for (let i = 0; i < 255; i++) {
    QR_EXP.push(value);
    QR_LOG[value] = i;
    value <<= 1;
    if (value & 0x100) {
      value ^= 0x11d;
    }
  }
  for (let i = 0; i < 255; i++) {
    QR_EXP.push(QR_EXP[i]);
  }
})();

function qrMultiply(a, b) {
  return (a === 0 || b === 0) ? 0 : QR_EXP[QR_LOG[a] + QR_LOG[b]];
}

// 誤り訂正語を作るための多項式
function qrGenerator(degree) {
  let poly = [1];
  for (let i = 0; i < degree; i++) {
    const next = [];
    for (let j = 0; j <= poly.length; j++) {
      next.push(0);
    }
    for (let j = 0; j < poly.length; j++) {
      next[j] ^= poly[j];
      next[j + 1] ^= qrMultiply(poly[j], QR_EXP[i]);
    }
    poly = next;
  }
  return poly;
}

// データ語から誤り訂正語を作る（多項式の余り）
function qrRemainder(data, degree) {
  const generator = qrGenerator(degree);
  const buffer = data.slice();
  for (let i = 0; i < degree; i++) {
    buffer.push(0);
  }
  for (let i = 0; i < data.length; i++) {
    const factor = buffer[i];
    if (factor === 0) {
      continue;
    }
    for (let j = 0; j < generator.length; j++) {
      buffer[i + j] ^= qrMultiply(generator[j], factor);
    }
  }
  return buffer.slice(data.length);
}

// BCH 符号。形式情報と型番情報の誤り訂正に使う。
function qrBch(value, poly, degree) {
  let rest = value << degree;
  const width = qrBitLength(poly);
  while (qrBitLength(rest) >= width) {
    rest ^= poly << (qrBitLength(rest) - width);
  }
  return rest;
}

function qrBitLength(value) {
  let bits = 0;
  while (value > 0) {
    bits++;
    value >>>= 1;
  }
  return bits;
}

// UTF-8 のバイト列にする。QR の 8 ビットモードはバイト列しか運べない。
function qrBytes(text) {
  const bytes = [];
  for (const character of text) {
    const code = character.codePointAt(0);
    if (code < 0x80) {
      bytes.push(code);
    } else if (code < 0x800) {
      bytes.push(0xc0 | (code >> 6), 0x80 | (code & 0x3f));
    } else if (code < 0x10000) {
      bytes.push(0xe0 | (code >> 12), 0x80 | ((code >> 6) & 0x3f), 0x80 | (code & 0x3f));
    } else {
      bytes.push(0xf0 | (code >> 18), 0x80 | ((code >> 12) & 0x3f),
                 0x80 | ((code >> 6) & 0x3f), 0x80 | (code & 0x3f));
    }
  }
  return bytes;
}

// 収まる型番を返す。入らなければ 0。
function qrVersionFor(length) {
  for (let version = 1; version <= QR_BLOCKS.length; version++) {
    const spec = QR_BLOCKS[version - 1];
    const words = spec[1] * spec[2] + spec[3] * spec[4];
    // モード 4 ビットと文字数（型番 10 からは 16 ビット）のぶんを引く
    if (length <= words - (version >= 10 ? 3 : 2)) {
      return version;
    }
  }
  return 0;
}

/// この文字列を QR にできるか。呼び出し側が先に知らせるために使う。
function qrFits(text) {
  return qrVersionFor(qrBytes(text).length) > 0;
}

// データ語を作る（誤り訂正の前）
function qrCodewords(data, version) {
  const spec = QR_BLOCKS[version - 1];
  const words = spec[1] * spec[2] + spec[3] * spec[4];
  const bits = [];
  const push = (value, count) => {
    for (let i = count - 1; i >= 0; i--) {
      bits.push((value >> i) & 1);
    }
  };
  push(4, 4);
  push(data.length, version >= 10 ? 16 : 8);
  for (const byte of data) {
    push(byte, 8);
  }
  // 終端の印。残りが 4 ビットに満たなければ、そのぶんだけ。
  for (let i = 0; i < 4 && bits.length < words * 8; i++) {
    bits.push(0);
  }
  while (bits.length % 8 !== 0) {
    bits.push(0);
  }

  const codewords = [];
  for (let i = 0; i < bits.length; i += 8) {
    let byte = 0;
    for (let j = 0; j < 8; j++) {
      byte = (byte << 1) | bits[i + j];
    }
    codewords.push(byte);
  }
  // 余りは決まった 2 つの値で埋める。必ず 0xec から始めて交互に置く。
  for (let i = 0; codewords.length < words; i++) {
    codewords.push(i % 2 === 0 ? 0xec : 0x11);
  }
  return codewords;
}

// ブロックに分けて誤り訂正語を付け、決まった順に混ぜ合わせる。
// 汚れが 1 か所に固まっても、複数のブロックに散るようにするため。
function qrInterleave(codewords, version) {
  const spec = QR_BLOCKS[version - 1];
  const blocks = [];
  const corrections = [];
  let at = 0;
  for (let group = 0; group < 2; group++) {
    const count = spec[1 + group * 2];
    const length = spec[2 + group * 2];
    for (let i = 0; i < count; i++) {
      const block = codewords.slice(at, at + length);
      at += length;
      blocks.push(block);
      corrections.push(qrRemainder(block, spec[0]));
    }
  }

  const stream = [];
  const longest = Math.max(spec[2], spec[4]);
  for (let i = 0; i < longest; i++) {
    for (const block of blocks) {
      if (i < block.length) {
        stream.push(block[i]);
      }
    }
  }
  for (let i = 0; i < spec[0]; i++) {
    for (const correction of corrections) {
      stream.push(correction[i]);
    }
  }
  return stream;
}

// マスクの式。読み取り機が迷わないよう、白黒の偏りを崩すために掛ける。
function qrMasked(mask, x, y) {
  switch (mask) {
    case 0: return (x + y) % 2 === 0;
    case 1: return y % 2 === 0;
    case 2: return x % 3 === 0;
    case 3: return (x + y) % 3 === 0;
    case 4: return (Math.floor(y / 2) + Math.floor(x / 3)) % 2 === 0;
    case 5: return (x * y) % 2 + (x * y) % 3 === 0;
    case 6: return ((x * y) % 2 + (x * y) % 3) % 2 === 0;
    default: return ((x + y) % 2 + (x * y) % 3) % 2 === 0;
  }
}

// 1 つのマスクで組み上げる
function qrDraw(stream, version, mask) {
  const size = version * 4 + 17;
  const modules = [];
  const fixed = [];
  for (let y = 0; y < size; y++) {
    const row = [];
    const flags = [];
    for (let x = 0; x < size; x++) {
      row.push(0);
      flags.push(false);
    }
    modules.push(row);
    fixed.push(flags);
  }

  // 位置を知らせるファインダ（角の三重の四角）と、その周りの空き
  const finder = (left, top) => {
    for (let dy = -1; dy <= 7; dy++) {
      for (let dx = -1; dx <= 7; dx++) {
        const x = left + dx;
        const y = top + dy;
        if (x < 0 || y < 0 || x >= size || y >= size) {
          continue;
        }
        const inside = dx >= 0 && dx <= 6 && dy >= 0 && dy <= 6;
        const ring = inside && (dx === 0 || dx === 6 || dy === 0 || dy === 6);
        const core = inside && dx >= 2 && dx <= 4 && dy >= 2 && dy <= 4;
        modules[y][x] = (ring || core) ? 1 : 0;
        fixed[y][x] = true;
      }
    }
  };
  finder(0, 0);
  finder(size - 7, 0);
  finder(0, size - 7);

  // 目盛り。升目の間隔を読み取り機に伝える。
  for (let i = 8; i < size - 8; i++) {
    const dark = i % 2 === 0 ? 1 : 0;
    modules[6][i] = dark;
    fixed[6][i] = true;
    modules[i][6] = dark;
    fixed[i][6] = true;
  }

  // 位置合わせ。歪みを直すために置く。
  const centers = QR_ALIGN[version - 1];
  const last = centers.length - 1;
  for (let i = 0; i <= last; i++) {
    for (let j = 0; j <= last; j++) {
      // 角の 3 つはファインダと重なる
      if ((i === 0 && j === 0) || (i === 0 && j === last) || (i === last && j === 0)) {
        continue;
      }
      for (let dy = -2; dy <= 2; dy++) {
        for (let dx = -2; dx <= 2; dx++) {
          const x = centers[j] + dx;
          const y = centers[i] + dy;
          modules[y][x] = Math.max(Math.abs(dx), Math.abs(dy)) === 1 ? 0 : 1;
          fixed[y][x] = true;
        }
      }
    }
  }

  // 形式情報と型番情報の場所を空けておく
  for (let i = 0; i < 9; i++) {
    fixed[8][i] = true;
    fixed[i][8] = true;
  }
  for (let i = 0; i < 8; i++) {
    fixed[8][size - 1 - i] = true;
    fixed[size - 1 - i][8] = true;
  }
  modules[size - 8][8] = 1;
  fixed[size - 8][8] = true;
  if (version >= 7) {
    for (let i = 0; i < 6; i++) {
      for (let j = 0; j < 3; j++) {
        fixed[i][size - 11 + j] = true;
        fixed[size - 11 + j][i] = true;
      }
    }
  }

  // データを右下から蛇行させて置く
  let at = 0;
  for (let right = size - 1; right >= 1; right -= 2) {
    if (right === 6) {
      right = 5;
    }
    for (let step = 0; step < size; step++) {
      for (let column = 0; column < 2; column++) {
        const x = right - column;
        const upward = ((right + 1) & 2) === 0;
        const y = upward ? size - 1 - step : step;
        if (fixed[y][x]) {
          continue;
        }
        let dark = at < stream.length * 8 ? (stream[at >> 3] >> (7 - (at & 7))) & 1 : 0;
        at++;
        if (qrMasked(mask, x, y)) {
          dark ^= 1;
        }
        modules[y][x] = dark;
      }
    }
  }

  // 形式情報（誤り訂正の水準とマスク）。2 か所に同じものを置く。
  const format = ((0 << 3) | mask);
  const formatBits = ((format << 10) | qrBch(format, 0x537, 10)) ^ 0x5412;
  for (let i = 0; i < 15; i++) {
    const dark = (formatBits >> i) & 1;
    if (i < 6) {
      modules[i][8] = dark;
    } else if (i === 6) {
      modules[7][8] = dark;
    } else if (i === 7) {
      modules[8][8] = dark;
    } else if (i === 8) {
      modules[8][7] = dark;
    } else {
      modules[8][14 - i] = dark;
    }
    if (i < 8) {
      modules[8][size - 1 - i] = dark;
    } else {
      modules[size - 15 + i][8] = dark;
    }
  }

  // 型番情報。型番 7 からは、大きさを別に知らせる。
  if (version >= 7) {
    const versionBits = (version << 12) | qrBch(version, 0x1f25, 12);
    for (let i = 0; i < 18; i++) {
      const dark = (versionBits >> i) & 1;
      const near = Math.floor(i / 3);
      const far = size - 11 + (i % 3);
      modules[near][far] = dark;
      modules[far][near] = dark;
    }
  }
  return modules;
}

// マスクの善し悪し。偏りや、ファインダと紛らわしい並びに点が付く。
// 小さいほど読み取りやすい。
function qrPenalty(modules) {
  const size = modules.length;
  let penalty = 0;
  const lines = [];
  for (let i = 0; i < size; i++) {
    let row = '';
    let column = '';
    for (let j = 0; j < size; j++) {
      row += modules[i][j];
      column += modules[j][i];
    }
    lines.push(row);
    lines.push(column);
  }
  for (const line of lines) {
    // 同じ色が 5 つ以上続く
    let run = 1;
    for (let i = 1; i <= line.length; i++) {
      if (i < line.length && line[i] === line[i - 1]) {
        run++;
        continue;
      }
      if (run >= 5) {
        penalty += 3 + (run - 5);
      }
      run = 1;
    }
    // ファインダに似た並び
    for (let i = 0; i + 11 <= line.length; i++) {
      const part = line.slice(i, i + 11);
      if (part === '10111010000' || part === '00001011101') {
        penalty += 40;
      }
    }
  }
  let dark = 0;
  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      dark += modules[y][x];
      // 同じ色の 2x2
      if (y + 1 < size && x + 1 < size
        && modules[y][x] === modules[y][x + 1]
        && modules[y][x] === modules[y + 1][x]
        && modules[y][x] === modules[y + 1][x + 1]) {
        penalty += 3;
      }
    }
  }
  // 黒の割合が半分から離れているほど重い
  const total = size * size;
  penalty += Math.floor(Math.abs(dark * 100 - total * 50) / (total * 5)) * 10;
  return penalty;
}

/**
 * 文字列を QR の升目にする。1 が黒。
 * 長すぎて入らないときは null を返す。
 */
function qrModules(text) {
  const data = qrBytes(text);
  const version = qrVersionFor(data.length);
  if (version === 0) {
    return null;
  }
  const stream = qrInterleave(qrCodewords(data, version), version);

  // マスクは 8 通りある。読み取りやすい並びになるものを選ぶ。
  let best = null;
  let bestPenalty = 0;
  for (let mask = 0; mask < 8; mask++) {
    const candidate = qrDraw(stream, version, mask);
    const penalty = qrPenalty(candidate);
    if (best === null || penalty < bestPenalty) {
      best = candidate;
      bestPenalty = penalty;
    }
  }
  return best;
}

// 名刺の組み方。
//
// 「テキスト」が書いた文字をそのまま画像にするのに対し、こちらは決まった
// 項目（画像・タイトル・サブタイトル・アカウント・QR）を受け取って並べる。
// 空の項目は場所を取らず、残ったものが詰まって真ん中に来る。
//
// 項目ごとに寄せや大きさを選ばせることはしない。名刺として見たときの
// 収まりはこの並べ方で決まっていて、そこを触れるようにすると、
// 電子ペーパーで読める組み方から外れるだけになる。

// タイトルに対する大きさ。英字表記やアカウントは名前より小さくする。
const SUBTITLE_RATIO = 0.55;
const ACCOUNT_RATIO = 0.45;

// 縦に積むときの取り分の重み。実際に使う高さは中身で決まり、
// 余ったぶんは詰めるので、ここは「どれを大きく見せるか」の目安でしかない。
const IMAGE_WEIGHT = 5;
const TEXT_WEIGHT = 3;
const QR_WEIGHT = 4;

// 横長のときに画像へ渡す幅の割合
const IMAGE_COLUMN = 0.45;

// 画像を枠に収める大きさ。縦横の比は変えない。
function fitImage(image, boxWidth, boxHeight) {
  const scale = Math.min(boxWidth / image.width, boxHeight / image.height);
  return {
    width: Math.max(1, Math.round(image.width * scale)),
    height: Math.max(1, Math.round(image.height * scale)),
  };
}

// 決めた大きさで文字を組んでみる。幅に入らなければ null。
// wrapping が false のときは、1 行に収まるかどうかだけを見る。
function layoutLines(ctx, texts, size, maxWidth, wrapping) {
  const rows = [];
  let height = 0;
  for (const item of texts) {
    if (item.text === '') {
      continue;
    }
    const own = Math.max(4, Math.round(size * item.ratio));
    ctx.font = fontOf(own);
    const wrapped = wrap(ctx, item.text, maxWidth);
    if (!wrapping && wrapped.lines.length > 1) {
      return null;
    }
    if (widest(ctx, wrapped.lines) > maxWidth) {
      return null;
    }
    const lineHeight = Math.ceil(own * 1.35);
    for (const line of wrapped.lines) {
      rows.push({ text: line, size: own, lineHeight: lineHeight });
    }
    height += wrapped.lines.length * lineHeight;
  }
  return { rows: rows, height: height, size: size };
}

// 枠に収まる最大の大きさを二分探索で決める。考え方はテキストと同じだが、
// こちらは 3 つの大きさが連動する（比は固定）ので、探すのはタイトルの大きさ。
function fitLines(ctx, texts, maxWidth, maxHeight, wrapping) {
  let low = 4;
  let high = Math.max(4, maxHeight);
  let best = null;
  while (low <= high) {
    const size = (low + high) >> 1;
    const block = layoutLines(ctx, texts, size, maxWidth, wrapping);
    if (block !== null && block.height <= maxHeight) {
      best = block;
      low = size + 1;
    } else {
      high = size - 1;
    }
  }
  return best;
}

// 名刺に載せる文字の組み方を決める。
//
// 折り返さずに入るならそちらを採る。日本語は語の切れ目が無いので、
// 折り返しを先に許すと「山田」「太郎」と名前を割ってでも字を大きくしてしまう。
// ただし、そのために読めない大きさになるなら折り返しに任せる。
// テキストの側と同じ考え方で、境目も同じ READABLE を使う。
//
// share は枠いっぱい（自動）に対する割合。小さくしたいときだけ 1 未満にする。
function fitCard(ctx, texts, maxWidth, maxHeight, share) {
  const single = fitLines(ctx, texts, maxWidth, maxHeight, false);
  const best = (single !== null && single.size >= READABLE)
    ? single
    : (fitLines(ctx, texts, maxWidth, maxHeight, true) || single);
  if (best === null) {
    return null;
  }

  // 枠いっぱいを上限に、指定の割合まで小さくする。ここもテキストと同じで、
  // 割合の指定で READABLE より小さくはしない。自動でそこまで小さくなる
  // 場合（文字が多いとき）は、収めるほうを優先する。
  const floor = Math.min(best.size, READABLE);
  const size = Math.max(floor, Math.round(best.size * share));
  if (size === best.size) {
    return best;
  }

  // 小さくすると 1 行に入る文字数が変わるので、折り返しは取り直す
  return layoutLines(ctx, texts, size, maxWidth, true) || best;
}

function paintLines(ctx, block, centerX, top) {
  ctx.fillStyle = '#000';
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  let y = top;
  for (const row of block.rows) {
    ctx.font = fontOf(row.size);
    ctx.fillText(row.text, centerX, y + row.lineHeight / 2);
    y += row.lineHeight;
  }
}

// QR の 1 升の大きさ。整数にする。電子ペーパーは階調が粗く、
// 半端な大きさで描くと升目の境が濁って読めなくなる。
function qrUnit(modules, side) {
  return Math.max(1, Math.floor(side / (modules.length + QUIET * 2)));
}

// QR を描く。周りの余白（クワイエットゾーン）も自分で持つ。
// これが無いと、読み取り機が符号の端を見つけられない。
function paintQr(ctx, modules, left, top, unit) {
  const side = (modules.length + QUIET * 2) * unit;
  ctx.fillStyle = '#fff';
  ctx.fillRect(left, top, side, side);
  ctx.fillStyle = '#000';
  for (let y = 0; y < modules.length; y++) {
    for (let x = 0; x < modules.length; x++) {
      if (modules[y][x]) {
        ctx.fillRect(left + (x + QUIET) * unit, top + (y + QUIET) * unit, unit, unit);
      }
    }
  }
}

/**
 * 名刺を描く。
 *
 * card は { image, title, subtitle, account, url }。
 * image は { element, width, height } で、読み込みは呼び出し側が済ませておく。
 * url は QR にする。長すぎて入らないものは qrFits で先に弾いておくこと。
 * share は文字の大きさ。枠いっぱい（自動）に対する割合で、1 なら自動のまま。
 *
 * 返り値は { drawn, size }。drawn が false なら枠に入らなかった。
 * size は実際に使ったタイトルの大きさ（文字が無ければ 0）。
 */
function paintCard(ctx, width, height, card, share) {
  ctx.fillStyle = '#fff';
  ctx.fillRect(0, 0, width, height);

  const texts = [
    { text: (card.title || '').trim(), ratio: 1 },
    { text: (card.subtitle || '').trim(), ratio: SUBTITLE_RATIO },
    { text: (card.account || '').trim(), ratio: ACCOUNT_RATIO },
  ];
  const image = card.image || null;
  const url = (card.url || '').trim();
  const modules = url === '' ? null : qrModules(url);

  const kinds = [];
  if (image !== null) {
    kinds.push('image');
  }
  if (texts[0].text !== '' || texts[1].text !== '' || texts[2].text !== '') {
    kinds.push('text');
  }
  if (modules !== null) {
    kinds.push('qr');
  }
  if (kinds.length === 0) {
    return { drawn: false, size: 0 };
  }

  // 余白。ベゼルに隠れる分と、名刺として見たときの見栄えの両方から取る。
  // テキストと同じ取り方にしてある。
  const padding = Math.round(Math.min(width, height) * 0.08);
  const gap = Math.round(Math.min(width, height) * 0.05);
  const innerWidth = Math.max(1, width - padding * 2);
  const innerHeight = Math.max(1, height - padding * 2);

  let titleSize = 0;

  // 1 列に積む。
  //
  // 画像と QR は重みで割った取り分に収め、文字はその残り全部をもらう。
  // 文字だけ先に決めると、行数の多い名前で取り分を超えて「入らない」に
  // なってしまう。実際には他が使わなかったぶんが空いている。
  const column = (list, left, columnWidth, top, columnHeight) => {
    const free = Math.max(1, columnHeight - gap * (list.length - 1));
    let weight = 0;
    for (const kind of list) {
      weight += kind === 'image' ? IMAGE_WEIGHT : (kind === 'qr' ? QR_WEIGHT : TEXT_WEIGHT);
    }

    const items = {};
    let taken = 0;
    for (const kind of list) {
      if (kind === 'image') {
        const allot = Math.max(1, Math.round(free * IMAGE_WEIGHT / weight));
        const box = fitImage(image, columnWidth, allot);
        items.image = { width: box.width, height: box.height };
        taken += box.height;
      } else if (kind === 'qr') {
        const allot = Math.max(1, Math.round(free * QR_WEIGHT / weight));
        const unit = qrUnit(modules, Math.min(columnWidth, allot));
        const side = (modules.length + QUIET * 2) * unit;
        items.qr = { width: side, height: side, unit: unit };
        taken += side;
      }
    }
    if (list.indexOf('text') >= 0) {
      const block = fitCard(ctx, texts, columnWidth, Math.max(1, free - taken), share);
      if (block === null) {
        return false;
      }
      titleSize = block.size;
      items.text = { width: columnWidth, height: block.height, block: block };
    }

    let used = gap * (list.length - 1);
    for (const kind of list) {
      used += items[kind].height;
    }
    let y = top + Math.max(0, Math.round((columnHeight - used) / 2));
    for (const kind of list) {
      const item = items[kind];
      const x = left + Math.round((columnWidth - item.width) / 2);
      if (kind === 'image') {
        ctx.drawImage(image.element, x, y, item.width, item.height);
      } else if (kind === 'text') {
        paintLines(ctx, item.block, left + columnWidth / 2, y);
      } else {
        paintQr(ctx, modules, x, y, item.unit);
      }
      y += item.height + gap;
    }
    return true;
  };

  // 横長で、画像とそれ以外があるときは、画像を左に置いて右に積む。
  // 1 列に積むと画像が潰れ、左右が空いたままになる。
  if (width > height && image !== null && kinds.length > 1) {
    const leftWidth = Math.round(innerWidth * IMAGE_COLUMN);
    const right = padding + leftWidth + gap;
    const rest = [];
    for (const kind of kinds) {
      if (kind !== 'image') {
        rest.push(kind);
      }
    }
    if (!column(['image'], padding, leftWidth, padding, innerHeight)
      || !column(rest, right, Math.max(1, width - padding - right), padding, innerHeight)) {
      return { drawn: false, size: 0 };
    }
  } else if (!column(kinds, padding, innerWidth, padding, innerHeight)) {
    return { drawn: false, size: 0 };
  }
  return { drawn: true, size: titleSize };
}

// 描くのはプレビューの canvas。大きさを整えるところだけこちらに置いて、
// 絵の中身は paintText / paintCard に閉じ込めてある。
function drawText(body, width, height, share, align, wrapping) {
  const ctx = context(width, height);
  drawnSize = paintText(ctx, preview.width, preview.height, body, share, align, wrapping);
}

// 名刺が枠に収まったか。収まらなければ送らせない。
let cardDrawn = false;

function drawCard(card, width, height, share) {
  const ctx = context(width, height);
  const result = paintCard(ctx, preview.width, preview.height, card, share);
  cardDrawn = result.drawn;
  drawnSize = result.size;
}

// 本体が受け取れる大きさに収まるまで、圧縮を強めながら小さくしていく。
// 試す回数が多いとスマホ側の負荷が大きいので、段階は絞ってある。
async function encode() {
  for (let step = 0; step < 3; step++) {
    render(Math.pow(0.7, step));

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

// 描き直してから、送れる形になるまで試す。画像とテキストで共通。
async function prepare() {
  send.disabled = true;
  blob = null;
  const encoded = await encode();
  preview.style.display = 'block';
  if (!encoded) {
    show('大きすぎて送れません', 'ng');
    return;
  }
  blob = encoded.data;
  name = 'image.' + encoded.ext;
  send.disabled = false;
  const size = drawnSize > 0 ? ' / 文字 ' + drawnSize + 'px' : '';
  show(preview.width + ' x ' + preview.height + ' / ' + Math.round(blob.size / 1024) + ' KB' + size);
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
    const base = Math.min(1, boxW / img.naturalWidth, boxH / img.naturalHeight);

    render = scale => drawImage(img, base * scale);
    await prepare();
  } catch (e) {
    show('この画像は読み込めませんでした', 'ng');
  }
});

function size(input, fallback) {
  const value = Math.round(Number(input.value));
  if (!isFinite(value) || value < 16) return fallback;
  return Math.min(value, 2000);
}

// 入力のたびに描き直すと、打っている間ずっと二分探索と PNG の生成が走る。
// 手が止まってからにする。
let pending = 0;
function scheduleText() {
  clearTimeout(pending);
  pending = setTimeout(async () => {
    const body = text.value;
    if (body.trim() === '') {
      preview.style.display = 'none';
      send.disabled = true;
      blob = null;
      show('');
      return;
    }
    show('作成中...');
    const width = size(tw, W);
    const height = size(th, H);
    render = scale => drawText(body, width * scale, height * scale, chosenShare(), alignment, reflow);
    await prepare();

    // 枠に対して文字が多すぎると、どの大きさでも収まらず何も描けない。
    // 白いままの画像を送ってしまわないよう、ここで止める。
    if (blob !== null && drawnSize === 0) {
      blob = null;
      send.disabled = true;
      show('文字が多すぎて枠に入りません', 'ng');
    }
  }, 300);
}

let cardPending = 0;
function scheduleCard() {
  clearTimeout(cardPending);
  cardPending = setTimeout(async () => {
    const card = {
      image: cardImage,
      title: cardTitle.value,
      subtitle: cardSubtitle.value,
      account: cardAccount.value,
      url: cardUrl.value.trim(),
    };
    const empty = card.image === null && card.url === ''
      && card.title.trim() === '' && card.subtitle.trim() === '' && card.account.trim() === '';
    if (empty) {
      preview.style.display = 'none';
      send.disabled = true;
      blob = null;
      show('');
      return;
    }

    // QR にできる長さには上限がある。描く前に知らせる。
    if (card.url !== '' && !qrFits(card.url)) {
      preview.style.display = 'none';
      send.disabled = true;
      blob = null;
      show('URL が長すぎて QR にできません', 'ng');
      return;
    }

    show('作成中...');
    const width = size(cardW, W);
    const height = size(cardH, H);
    render = scale => drawCard(card, width * scale, height * scale, cardShare());
    await prepare();

    // 文字が多すぎると、どの大きさでも収まらず何も描けない。
    if (blob !== null && !cardDrawn) {
      blob = null;
      send.disabled = true;
      show('文字が多すぎて枠に入りません', 'ng');
    }
  }, 300);
}

cardfile.addEventListener('change', async () => {
  const f = cardfile.files[0];
  if (!f) return;
  show('読み込み中...');
  try {
    const img = await load(f);
    cardImage = { element: img, width: img.naturalWidth, height: img.naturalHeight };
    cardClear.hidden = false;
    scheduleCard();
  } catch (e) {
    show('この画像は読み込めませんでした', 'ng');
  }
});

cardClear.addEventListener('click', () => {
  cardImage = null;
  cardfile.value = '';
  cardClear.hidden = true;
  scheduleCard();
});

for (const input of [cardTitle, cardSubtitle, cardAccount, cardUrl]) {
  input.addEventListener('input', scheduleCard);
}
cardW.addEventListener('change', scheduleCard);
cardH.addEventListener('change', scheduleCard);

// 自動で決めた大きさに対する割合。テキストの側と同じ扱いにしてある。
function cardShare() {
  const value = Number(cardRatio.value);
  return (isFinite(value) && value >= 40 && value <= 100) ? value / 100 : 1;
}

cardRatio.addEventListener('input', () => {
  const percent = Math.round(cardShare() * 100);
  document.getElementById('card-ratio-value').textContent =
    percent === 100 ? '自動' : ('自動の ' + percent + '%');
  scheduleCard();
});

document.getElementById('card-swap').addEventListener('click', () => {
  const width = cardW.value;
  cardW.value = cardH.value;
  cardH.value = width;
  scheduleCard();
});

// 自動で決めた大きさに対する割合。100% が枠いっぱいで、それより大きくはできない。
function chosenShare() {
  const value = Number(ratio.value);
  return (isFinite(value) && value >= 40 && value <= 100) ? value / 100 : 1;
}

text.addEventListener('input', scheduleText);
tw.addEventListener('change', scheduleText);
th.addEventListener('change', scheduleText);

ratio.addEventListener('input', () => {
  const percent = Math.round(chosenShare() * 100);
  document.getElementById('ratio-value').textContent =
    percent === 100 ? '自動' : ('自動の ' + percent + '%');
  scheduleText();
});

document.getElementById('reflow').addEventListener('click', event => {
  const button = event.target.closest('button');
  if (!button) return;
  reflow = button.dataset.reflow;
  for (const other of document.getElementById('reflow').getElementsByTagName('button')) {
    other.className = (other === button) ? 'on' : '';
  }
  scheduleText();
});

document.getElementById('align').addEventListener('click', event => {
  const button = event.target.closest('button');
  if (!button) return;
  alignment = button.dataset.align;
  for (const other of document.getElementById('align').getElementsByTagName('button')) {
    other.className = (other === button) ? 'on' : '';
  }
  scheduleText();
});

document.getElementById('swap').addEventListener('click', () => {
  const width = tw.value;
  tw.value = th.value;
  th.value = width;
  scheduleText();
});

// 行き来しても、送るのは最後に作ったものだけにする。
// 切り替えた時点でプレビューを捨てて、作り直してもらう。
const MODES = ['image', 'text', 'card'];
function select(mode) {
  for (const name of MODES) {
    document.getElementById('pane-' + name).hidden = name !== mode;
    document.getElementById('tab-' + name).className = name === mode ? 'on' : '';
  }

  clearTimeout(pending);
  clearTimeout(cardPending);
  blob = null;
  render = null;
  send.disabled = true;
  preview.style.display = 'none';
  show('');
  if (mode === 'text') { scheduleText(); }
  if (mode === 'card') { scheduleCard(); }
}

for (const mode of MODES) {
  document.getElementById('tab-' + mode).addEventListener('click', () => select(mode));
}

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
                                ? "Tap the screen, or hold BTN(IO21), to go back to the list."
                                : "BTN(IO21): hold = back to the list";
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
