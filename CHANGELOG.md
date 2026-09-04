# 変更履歴

## 未リリース

- Microsoft Defender の `tasopatch.exe` に対する誤検出 (`Trojan:Win32/Wacatac.B!ml`) が
  2026-09-04 に解除された。README のウイルス対策ソフトの節を更新し、キャッシュに
  古い判定が残っている場合の対処 (`MpCmdRun.exe -removedefinitions -dynamicsignatures`)
  を追記。バイナリは v1.0.0 のまま変更なし。

## 1.0.0 — 2026-09-03

初版。

- `tasoavi.dll` — AVIFile API のラッパー。ゲームが要求するトップダウン DIB を
  ボトムアップ要求に変換して OS に渡し、返ってきた画像の行順を反転して返す。
  Windows 10 / 11 でムービーが再生できない問題を、ゲーム本体を改造せずに解決する。
- `tasopatch.exe` — GUI パッチャ。ドラッグ&ドロップ / 参照 / 自動検出の3経路に対応。
- `tools/patch_exe.py` — 同じことをするコマンドライン版。
- `tools/convert_movies.py` — Indeo 5 のムービーを YUY2（無圧縮）に変換する。
  ffmpeg には生フレームだけ出させ、単一 RIFF の AVI を自前で書く
  （OpenDML 分割された AVI は Windows の AVIFile が最後まで読めないため）。
- `tools/switch_movies.py` — indeo / yuy2 / cvid の切り替え。AVI ヘッダの FOURCC で
  現役の版を判定する。
- `tools/remove_indeo.bat` — Indeo の VFW 登録・DirectShow フィルタ登録の解除と、
  コーデック本体の退避。`vidc.yvu9` を Windows 標準の `tsbyuv.dll` に戻す処理を含む。
