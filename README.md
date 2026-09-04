# tasoavi — 誰彼 -たそがれ- ムービー再生修正

Windows 10 / 11 で `mov\*.avi` が再生できず

> AVIが描画できません。
> Codecがインストールされていないと思われます。
> CD内の[iv5setup.exe]を実行して、インストール作業を行って下さい。

で止まる問題を直します。**コーデックの問題ではありません。** ゲームが AVIFile API に
要求する画像形式（トップダウン DIB）を Windows 10 / 11 が受け付けなくなったのが原因で、
ムービーファイルを作り直しても直りません。詳しい話は [docs/TECHNICAL.md](docs/TECHNICAL.md) にあります。

`tasoavi.dll` は AVIFile API を中継し、その要求の食い違いだけを吸収します。
ゲーム本体の描画コードには一切手を入れません。

---

## 入手

**[Releases](../../releases) から zip をダウンロードしてください。**
このリポジトリにはビルド済みバイナリを置いていません（ソースと zip の中身が
食い違わないようにするためです）。自分でビルドすることもできます（下記「ビルド」）。

## 必要なもの

- Windows 10 / 11
- 導入だけなら追加のソフトは不要です
- ムービーの変換をする場合のみ Python 3 と ffmpeg

---

## 導入

### 1. `tasoavi.dll` を置く

ゲーム本体（`誰彼 -たそがれ-.exe`）と同じフォルダに `tasoavi.dll` をコピーします。

### 2. exe のインポート名を書き換える

`tasopatch.exe` を起動し、**`誰彼 -たそがれ-.exe` かゲームのフォルダをウィンドウに
ドラッグ&ドロップ**して、[パッチ適用] を押します。

- ゲームフォルダに置いて起動すれば、対象は自動で見つかります
- exe のアイコンにファイルを落としても構いません
- [参照...] から選ぶこともできます
- 管理者権限は要りません

これで exe のインポートが `AVIFIL32.dll` → `tasoavi.dll` に変わります。
`_inmm.dll` の導入と同じやり方です。書き換えるのは DLL 名の 13 バイトだけで、
ファイルの長さも他の構造も変わりません。初回に `.bak` が作られます。

元に戻すときは同じ画面で [元に戻す] を押してください。

> コマンドラインで済ませたい場合は Python 版もあります。
> ```
> python tools\patch_exe.py "誰彼 -たそがれ-.exe"
> python tools\patch_exe.py "誰彼 -たそがれ-.exe" --check
> python tools\patch_exe.py "誰彼 -たそがれ-.exe" --revert
> ```

### 3. 動作確認

ゲームを起動してタイトルとオープニングのムービーが出れば成功です。
ウィンドウモードとフルスクリーンの両方で確認しておくと確実です。

---

## ムービーファイルについて（任意・推奨）

このラッパーだけ入れれば **オリジナルの Indeo 5 ファイルはそのまま再生できます**
（Indeo 5.11 が登録済みの環境なら）。

ただし Indeo は署名なし・2000 年で更新停止・Microsoft が Windows Vista 以降で
ロードを禁止した代物です（[Security Advisory 954157](https://learn.microsoft.com/en-us/security-updates/securityadvisories/2009/954157)）。
`YUY2`（無圧縮）に変換して Indeo を丸ごと外すことを勧めます。

```
python tools\convert_movies.py --ffmpeg "C:\path\to\ffmpeg.exe"
```

`indeo` / `yuy2` / `cvid`（Cinepak）の3つを置いておいて切り替えられます。

```
python tools\switch_movies.py          状態表示
python tools\switch_movies.py yuy2     YUY2 に切り替え
python tools\switch_movies.py cvid     Cinepak に切り替え
python tools\switch_movies.py indeo    原版に戻す
```

| 形式 | 画質 (32bpp 要求時 PSNR) | 4本合計 | デコーダ |
|---|---:|---:|---|
| YUY2 | 36.6 dB | 2.34 GiB | `msyuv.dll`（Microsoft 署名） |
| Cinepak | 31.9 dB | 約 175 MB | `iccvid.dll`（Microsoft 署名） |
| Indeo 5（原版） | — | 約 195 MB | `ir50_32.dll`（署名なし） |

容量が気になるなら Cinepak、画質を採るなら YUY2 です。選び方の根拠は
[docs/TECHNICAL.md](docs/TECHNICAL.md) にあります。

### Indeo の除去

差し替え後に動作確認できたら、**管理者権限で** `tools\remove_indeo.bat` を実行してください。
VFW 登録だけでなく DirectShow フィルタとして登録されている 23 個の CLSID も解除します。
レジストリはバックアップを取り、DLL は削除せず `indeo_backup` に退避します。
何度実行しても安全です。

---

## 元に戻す

1. `tasopatch.exe` で [元に戻す]（または `.bak` を書き戻す）
2. `tasoavi.dll` を削除
3. ムービーを変換していれば `python tools\switch_movies.py indeo`
4. Indeo を外していれば `indeo_backup` の `.reg` を戻し、DLL を元の場所に戻す

---

## ウイルス対策ソフトについて

自前ビルドの署名なしバイナリは、**出回っていないという理由だけで**検出されることが
あります。検出名の末尾が `!ml` だったり `Gen:Variant` のような総称名の場合は、
挙動ではなく機械学習の推定によるものです。

| ファイル | サイズ | SHA-256 |
|---|---:|---|
| `tasoavi.dll` | 86,016 | `315b4321d36ef5dfefdd67c2698f2b4be2c3cc10f56c1b2846a4baf9ee140f45` |
| `tasopatch.exe` | 120,832 | `5a1d297571f309382c0368741cc894f834f688e4f4c1635205b1d3f0430b658e` |

VirusTotal のスキャン結果（リリース時点。定義更新で変わるのでリンク先が最新です）:

| ファイル | 検出 | 内訳 |
|---|---|---|
| [tasoavi.dll](https://www.virustotal.com/gui/file/315b4321d36ef5dfefdd67c2698f2b4be2c3cc10f56c1b2846a4baf9ee140f45) | 1 / 71 | Cynet（ML スコアラ） |
| [tasopatch.exe](https://www.virustotal.com/gui/file/5a1d297571f309382c0368741cc894f834f688e4f4c1635205b1d3f0430b658e) | 1 / 70 | SecureAge（ML スコアラ） |

いずれも機械学習ベースのエンジンのみで、パターンマッチ系のエンジンは1つも反応していません。

### Microsoft Defender の検出は解除済み

`tasopatch.exe` は当初 Microsoft Defender にも `Trojan:Win32/Wacatac.B!ml` として検出されていましたが、
誤検出として申告した結果、**2026-09-04 に Microsoft が検出を削除しました。**

> At this time, the submitted files do not meet our criteria for malware or potentially
> unwanted applications. The detection has been removed.

それでも古い判定がキャッシュに残って検出される場合は、Microsoft の案内どおり
**管理者権限のコマンドプロンプト**で次を実行してください。

```
cd "C:\Program Files\Windows Defender"
MpCmdRun.exe -removedefinitions -dynamicsignatures
MpCmdRun.exe -SignatureUpdate
```

`SHA256SUMS.txt` で同梱物すべてのハッシュを確認できます（PowerShell なら
`Get-FileHash tasoavi.dll -Algorithm SHA256`）。

検出された場合はゲームフォルダをリアルタイム検索の除外に追加してください。
Microsoft Defender の誤検出は https://www.microsoft.com/en-us/wdsi/filesubmission
から報告できます。

このソフトは誤検出の主因になる要素を意図的に**避けて**あります。

- OS 標準 DLL と同名にしない（`avifil32.dll` を名乗らない = DLL サイドローディングの形を取らない）
- アセンブラのトランポリン（`jmp` スタブ）を使わない
- `LoadLibrary` / `GetProcAddress` による実行時の関数解決をしない（すべて静的インポート）
- `DllMain` で何もしない
- バージョンリソースを持たせる
- `tasopatch.exe` は管理者権限を要求しない（マニフェストで `asInvoker`）
- ソースを同梱し、誰でも同じものをビルドできるようにする

---

## 同梱物

```
README.md                この文書
CHANGELOG.md             変更履歴
LICENSE.txt              ライセンス
SHA256SUMS.txt           同梱物のハッシュ
tasoavi.dll              本体 (32bit)
tasopatch.exe            GUI パッチャ
tools/
    patch_exe.py         パッチャ (コマンドライン版)
    convert_movies.py    ムービー変換
    switch_movies.py     ムービー切り替え
    remove_indeo.bat     Indeo の登録解除と退避 (要管理者権限)
src/
    tasoavi.c  tasoavi.def  tasoavi.rc
    tasopatch.c  tasopatch.rc  tasopatch.manifest
    build.bat            ビルド用
docs/
    TECHNICAL.md         なぜこうなっているのかの詳細
```

## ビルド

Visual Studio の「C++ によるデスクトップ開発」ワークロードが必要です。

```
src\build.bat
```

`tasoavi.dll`（32bit）と `tasopatch.exe` がトップレベルに出力されます。

配布用の zip を組み立てるには `make_release.py` を使います（ソースより古いバイナリが
残っていると止まります）。

```
python make_release.py --version 1.0.0
```

## ライセンス

[MIT License](LICENSE.txt) — Copyright (c) 2026 AlfortVanilla (j_arayan)

このライセンスが及ぶのは tasoavi 自身のソースコードと、そこからビルドされる
`tasoavi.dll` / `tasopatch.exe`、および同梱のスクリプトと文書だけです。

「誰彼 -たそがれ-」およびそのデータ（実行ファイル、`*.pak`、`mov` フォルダの動画など）
の著作権は株式会社アクアプラス / Leaf に帰属します。本配布物にはそれらを一切含んでおらず、
再配布物に含めることも認められません。パッチはあなたが正規に所有するゲームに対して、
あなたの手元で適用するものです。
