# tasoavi — 技術的な話

導入手順は [../README.md](../README.md) にあります。この文書は「なぜこうなっているのか」だけを扱います。

---

## 1. 何が起きているのか

ゲーム本体（`0x00482D50` 付近）は `AVIStreamGetFrameOpen()` に対して
**トップダウン DIB**（`BITMAPINFOHEADER.biHeight` が負）を要求します。

```
biSize        = 0x28
biWidth       = 幅
biHeight      = -高さ      ← これ
biPlanes      = 1
biBitCount    = 24
biCompression = BI_RGB
biSizeImage   = 幅 * 高さ * 3
```

Windows 9x / 2000 / XP の `avifil32.dll` はこの要求を受け付けましたが、
**Windows 10 / 11 では `biHeight` が負というだけで NULL が返ります。**

コーデックとは無関係です。実測:

| 要求形式 | 結果 |
|---|---|
| 320x240 24bpp **top-down**（ゲームと同じ） | **FAIL** |
| 320x240 24bpp bottom-up | OK |
| 320x240 32 / 16 / 8bpp bottom-up | OK |
| `ICDecompressQuery` を Indeo 5 に直接投げる (top-down) | OK ← コーデック自体は対応している |

無圧縮 AVI でも DV でも同じく失敗します。つまり **AVI ファイルを作り直しても直りません。**

ゲームは1回目の要求が失敗するとストリーム本来の形式で開き直そうとしますが、
そちらもトップダウン要求のままなので再び失敗し、エラーメッセージを出して停止します。

### これは「仕様変更」ではない

Microsoft がこの挙動の変更を告知した文書は見つかりません。おそらく存在しません。
ただし **ゲーム側の要求がもともと文書化された契約の外だった** ことは資料で裏が取れます。

**(a) `lpbiWanted` に指定できる値として文書化されているのは2つだけ**

> Pointer to a structure that defines the desired video format. Specify **NULL** to use a
> default format. You can also specify AVIGETFRAMEF_BESTDISPLAYFMT to decode the frames to
> the best format for your display.
>
> — [AVIStreamGetFrameOpen function (vfw.h)](https://learn.microsoft.com/en-us/windows/win32/api/vfw/nf-vfw-avistreamgetframeopen)

自作の `BITMAPINFOHEADER` を渡すことは書かれていません。したがって Microsoft は
この使い方の互換性を保つ義務を負っておらず、変更しても breaking change として告知しません。

**(b) トップダウン出力は「拡張メッセージ」の機能として定義されている**

> The ICM_DECOMPRESSEX message notifies a video compression driver to decompress a frame of
> data directly to the screen, **decompress to an upside-down DIB**, or decompress images
> described with source and destination rectangles.
>
> — [ICM_DECOMPRESSEX message](https://learn.microsoft.com/en-us/windows/win32/multimedia/icm-decompressex)

上下逆（トップダウン）DIB へのデコードは素の `ICM_DECOMPRESS` ではなく
`ICM_DECOMPRESSEX` の機能です。`avifil32` の GetFrame が素の経路を使う限り、
トップダウン要求を弾くのは仕様に沿った動作と言えます。

**(c) `biHeight` の規定**

> For uncompressed RGB bitmaps, if biHeight is positive, the bitmap is a bottom-up DIB...
> If biHeight is negative, the bitmap is a top-down DIB...
> For YUV bitmaps, the bitmap is always top-down, **regardless of the sign of biHeight**.
> For compressed formats, biHeight must be positive, regardless of image orientation.
>
> — [BITMAPINFOHEADER (wingdi.h)](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-bitmapinfoheader)

**(d)** Video for Windows / DirectShow はどちらも legacy 宣言済みです
（[Video for Windows](https://learn.microsoft.com/en-us/windows/win32/multimedia/video-for-windows)）。

つまり **XP まで通っていたのは実装が寛容だっただけで、10 / 11 が仕様どおりになった**、
というのが正確な言い方です。

参考: Wine と ReactOS の `avifil32` は `lpbiWanted` を検証せずそのまま `ICLocate` に
渡していて、負の高さを特別扱いしません（[ReactOS: avifil32/api.c](https://doxygen.reactos.org/db/d8f/dll_2win32_2avifil32_2api_8c_source.html)）。
昔の実装が素通ししていたことの傍証にはなりますが、Microsoft のコードではありません。

---

## 2. このDLLがやること

ゲームが使う AVIFile API を中継します。ほとんどはそのまま OS に渡し、
フレーム取得の3本だけ次のように振る舞います。

| API | 動作 |
|---|---|
| `AVIStreamGetFrameOpen` | `biHeight` が負なら正（ボトムアップ）に直して OS に渡す |
| `AVIStreamGetFrame` | 返ってきた画像の行順を反転し、`biHeight` を負に戻して返す |
| `AVIStreamGetFrameClose` | 上記の作業領域を解放する |

ゲームは要求どおりのトップダウン DIB を受け取るので、描画コードは無改造で動きます。
出力はボトムアップ経路でデコードした結果と**ピクセル単位で完全一致**することを確認済みです。

### 安全側に倒す設計

`AVIStreamGetFrame` が返してくるヘッダはデコーダ（＝サードパーティのコーデックが
登録され得る場所）由来なので、値を信用しません。次のいずれかに当てはまる場合は
**行を反転せず、OS が返したものをそのまま返します**。

- `biCompression` が `BI_RGB` / `BI_BITFIELDS` 以外
  （YUV の DIB は `biHeight` の符号によらず常にトップダウンなので、反転すると壊れる）
- 高さが 0 以下、または 65535 を超える
- 幅が 0 以下、または 65535 を超える
- ビット深度が 0、または 32 を超える
- `biSize` が 4096 を超える、パレットが 256 エントリを超える
- 必要バッファが 64 MiB を超える（`stride × height` は 64bit で計算して桁あふれを防ぐ）
- `realloc` に失敗した

ゲームが実際に要求するのは最大 640x352x4 ≒ 0.9 MB なので、通常経路がこれらに
触れることはありません。

`tasopatch.exe` の PE パーサも同様に、すべてのオフセットとサイズを桁あふれなしで
範囲検査してから参照します（利用者が任意のファイルをドロップできるため）。

---

## 3. なぜ YUY2 なのか

ゲームは `GetDeviceCaps(BITSPIXEL)` の値をそのままデコード要求のビット深度に使います
（`0x00482D50` で `(v+1)*8` を計算）。Windows 11 のデスクトップは 32bpp なので、
**32bpp 要求で正しく動く形式**でなければいけません。同一フレームでの実測:

| 形式 | デコーダ | 32bpp | 24bpp | 16bpp | 4本合計 |
|---|---|---:|---:|---:|---:|
| **YUY2（無圧縮）** | `msyuv.dll`（MS 署名） | **36.6 dB** | 35.8 | 21.6 | 2.34 GiB |
| Cinepak | `iccvid.dll`（MS 署名） | 31.9 dB | 31.3 | 28.1 | 約 175 MB |
| I420（無圧縮） | `iyuv_32.dll` | **2.9 dB（破綻）** | 36.0 | 31.3 | 1.9 GiB |
| YVU9（無圧縮） | `iyvu9_32.dll` | 開けない | 10.2 | 10.2 | 1.4 GiB |
| 無圧縮 RGB24 | なし | 開けない | 開けない | 開けない | 3.8 GiB |

Indeo 5 の内部形式は 4:1:0（`yuv410p`）なので、4:2:2 の YUY2 は彩度を落とさず
そのまま収まります。差分の大半は YUV→RGB の変換係数の違いによるものです。

音声ストリームは付けません（ゲームは音声ストリーム付き AVI を弾きます。原版も映像のみ）。

### Cinepak 版

YUY2 版は 640x352/30fps で毎秒 13.5 MB を読み続けます。ディスクに優しくしたいなら
Cinepak 版のほうが原版とほぼ同じ容量で収まります（画質は約 5 dB 落ちます）。

```
ffmpeg -i mov\Title320_15.avi.indeo -an -c:v cinepak -pix_fmt rgb24 -f avi mov\Title320_15.avi.cvid
```

Cinepak は `-b:v` を無視するため、ビットレートでの品質調整はできません。
`-max_extra_cb_iterations` などを上げても 2 倍遅くなって 1% 未満しか変わりませんでした。
出力は 1GB を超えないので ffmpeg の AVI マルチプレクサをそのまま使えます。
エンコードは非常に遅く、`Tasogare640_30` で約 50 分かかります。

### なぜ AVI を自前で書いているのか

ffmpeg の AVI マルチプレクサは 1GB ごとに RIFF を分割して OpenDML AVI にします。
**Windows の AVIFile は分割境界より後ろのフレームを読めません**
（実測: frame 2000 は取得できるが 2400 以降は失敗）。
`Tasogare640_30` は YUY2 で 1.65 GiB になるため、ffmpeg には生フレームだけ出させ、
単一 RIFF の AVI を `convert_movies.py` 側で書いています。
1.65 GiB はレガシー AVI の 2GB 上限に収まります。

### 切り替えの判定

現役の版は `<名前>.avi`、控えは `<名前>.avi.<種別>` という名前で同居します。
どれが現役かは**ファイル名ではなく AVI ヘッダの FOURCC を読んで**判定します。また
RIFF のサイズ欄と実ファイルサイズが合わないもの（変換途中など）には切り替えません。

---

## 4. Indeo の除去

`remove_indeo.bat` は VFW 登録（`Drivers32`）だけでなく、**DirectShow フィルタとして
登録されている 23 個の CLSID** も `regsvr32 /u` と明示的なキー削除の両方で解除します。
アンインストーラが取りこぼした孤児エントリも掃除できます。

処理内容:

1. `Drivers32` / `drivers.desc` / 該当 CLSID をレジストリからエクスポート
2. DirectShow / ActiveMovie フィルタの登録解除
3. 残った COM 登録の削除
4. VFW 登録（`vidc.iv50` ほか）と `drivers.desc` 項目の削除
5. `vidc.yvu9` を Windows 標準の `tsbyuv.dll` に戻す
6. コーデック本体を `indeo_backup` へ退避

### 5 について

Windows は `vidc.yvu9` を自前の署名済み `tsbyuv.dll` に向けています（64bit 側は今もそう）。
Indeo のインストーラはこの 32bit 側を `iyvu9_32.dll` に書き換え、**アンインストーラは
元に戻さず値ごと削除**するため、32bit アプリから YVU9 ハンドラが消えた状態になります。
「Indeo を指している」場合と「値が無い」場合の両方を直します。

### 6 について

2つのシステムフォルダには**同名で中身の違うビルド**が入っています
（SysWOW64 = 2000 年の Ligos ビルド、System32 = 再パッケージ版）。取り違えないよう
それぞれ別のサブフォルダに退避します。System32 側は SYSTEM / HIDDEN 属性が付いていて
素の `move` では動かせないので、`attrib -s -h -r` してから移動し、移動できたかを
1件ずつ確認します。

インストーラが相手の DLL をリネームして残した `ir50_32.dll.2000` のような残骸も対象です。

移動に失敗しても実害はありません。64bit の System32 にある 32bit DLL は 64bit プロセスが
ロードできず、32bit プロセスは SysWOW64 にリダイレクトされるためです。レジストリからも
参照は消えています。

---

## 5. 参考資料

- [AVIStreamGetFrameOpen function (vfw.h)](https://learn.microsoft.com/en-us/windows/win32/api/vfw/nf-vfw-avistreamgetframeopen)
- [ICM_DECOMPRESSEX message](https://learn.microsoft.com/en-us/windows/win32/multimedia/icm-decompressex)
- [ICDecompressEx function (vfw.h)](https://learn.microsoft.com/en-us/windows/win32/api/vfw/nf-vfw-icdecompressex)
- [BITMAPINFOHEADER (wingdi.h)](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-bitmapinfoheader)
- [Bitmap Header Types](https://learn.microsoft.com/en-us/windows/win32/gdi/bitmap-header-types)
- [Video for Windows](https://learn.microsoft.com/en-us/windows/win32/multimedia/video-for-windows)
- [Microsoft Security Advisory 954157 — Indeo codec](https://learn.microsoft.com/en-us/security-updates/securityadvisories/2009/954157)
- [ReactOS: dll/win32/avifil32/api.c](https://doxygen.reactos.org/db/d8f/dll_2win32_2avifil32_2api_8c_source.html)
