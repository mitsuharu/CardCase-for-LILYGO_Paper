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
<p class="lead">選んだ画像、または書いた文字を表示します。</p>
<div class="tabs">
<button type="button" id="tab-image" class="on">画像</button>
<button type="button" id="tab-text">テキスト</button>
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

// 描くのはプレビューの canvas。大きさを整えるところだけこちらに置いて、
// 絵の中身は paintText に閉じ込めてある。
function drawText(body, width, height, share, align, wrapping) {
  const ctx = context(width, height);
  drawnSize = paintText(ctx, preview.width, preview.height, body, share, align, wrapping);
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

// 画像とテキストを行き来しても、送るのは最後に作ったものだけにする。
// 切り替えた時点でプレビューを捨てて、選び直してもらう。
function select(mode) {
  const isText = mode === 'text';
  document.getElementById('pane-image').hidden = isText;
  document.getElementById('pane-text').hidden = !isText;
  document.getElementById('tab-image').className = isText ? '' : 'on';
  document.getElementById('tab-text').className = isText ? 'on' : '';

  clearTimeout(pending);
  blob = null;
  render = null;
  send.disabled = true;
  preview.style.display = 'none';
  show('');
  if (isText) { scheduleText(); }
}

document.getElementById('tab-image').addEventListener('click', () => select('image'));
document.getElementById('tab-text').addEventListener('click', () => select('text'));

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
