#!/usr/bin/env python3
"""
mov フォルダのムービーを Indeo / YUY2 / Cinepak で切り替えます。

  python switch_movies.py                 現在の状態を表示
  python switch_movies.py yuy2            YUY2 版に切り替え
  python switch_movies.py cvid            Cinepak 版に切り替え
  python switch_movies.py indeo           オリジナル(Indeo 5)に戻す

いま使われている版は `<名前>.avi`、使っていない版は `<名前>.avi.<種別>` という
名前で同じフォルダに置かれます。どれが現役かはファイル名ではなく AVI ヘッダの
biCompression (FOURCC) を読んで判定するので、名前がずれていても取り違えません。
"""
import argparse
import os
import struct
import sys

MOVIES = ["Title320_15", "Title640_30", "Tasogare320_15", "Tasogare640_30"]

# 種別 -> (ストリームの FOURCC, 説明)
KINDS = {
    "indeo": (b"IV50", "Indeo 5 (原版・要 ir50_32.dll)"),
    "yuy2":  (b"YUY2", "YUY2 無圧縮 (msyuv.dll / Windows 標準)"),
    "cvid":  (b"cvid", "Cinepak (iccvid.dll / Windows 標準)"),
}
FOURCC_TO_KIND = {v[0]: k for k, v in KINDS.items()}


def read_fourcc(path):
    """AVI の映像ストリームの biCompression を返す。読めなければ None。"""
    try:
        with open(path, "rb") as f:
            head = f.read(0x1000)
    except OSError:
        return None
    if head[:4] != b"RIFF" or head[8:12] != b"AVI ":
        return None
    i = head.find(b"strf")
    if i < 0:
        return None
    return head[i + 8 + 16:i + 8 + 20]      # BITMAPINFOHEADER.biCompression


def is_complete(path):
    """RIFF のサイズ欄と実ファイルサイズが一致するか。

    変換途中のファイルに切り替えてしまう事故を防ぐための最低限の検査。
    """
    try:
        size = os.path.getsize(path)
        with open(path, "rb") as f:
            head = f.read(12)
        if head[:4] != b"RIFF" or head[8:12] != b"AVI ":
            return False
        declared = struct.unpack_from("<I", head, 4)[0] + 8
        return declared == size
    except OSError:
        return False


def describe(fourcc):
    if fourcc is None:
        return "?"
    kind = FOURCC_TO_KIND.get(fourcc)
    name = fourcc.decode("latin1", "replace")
    return "%s (%s)" % (kind, name) if kind else name


def status(movdir):
    print("%-16s %-26s %s" % ("", "現役 (.avi)", "控え"))
    print("-" * 78)
    for name in MOVIES:
        active = os.path.join(movdir, name + ".avi")
        if os.path.exists(active):
            cur = "%-14s %8.1f MB" % (describe(read_fourcc(active)),
                                      os.path.getsize(active) / 1e6)
        else:
            cur = "(ありません)"
        spare = []
        for k in KINDS:
            p = os.path.join(movdir, name + ".avi." + k)
            if os.path.exists(p):
                mark = "" if is_complete(p) else " !未完成"
                spare.append("%s %.0fMB%s" % (k, os.path.getsize(p) / 1e6, mark))
        print("%-16s %-26s %s" % (name, cur, ", ".join(spare) or "-"))


def switch(movdir, want):
    want_fourcc = KINDS[want][0]
    planned = []

    for name in MOVIES:
        active = os.path.join(movdir, name + ".avi")
        target = os.path.join(movdir, name + ".avi." + want)

        cur_fourcc = read_fourcc(active) if os.path.exists(active) else None
        if cur_fourcc == want_fourcc:
            print("  %-16s すでに %s です" % (name, want))
            continue
        if not os.path.exists(target):
            print("  %-16s [ERROR] %s が見つかりません" % (name, os.path.basename(target)))
            return 2
        if not is_complete(target):
            print("  %-16s [ERROR] %s が未完成です (RIFF のサイズ欄と実サイズが不一致)。"
                  "変換中なら終わるまで待ってください。" % (name, os.path.basename(target)))
            return 2
        if read_fourcc(target) != want_fourcc:
            print("  %-16s [ERROR] %s の中身が %s ではありません (%s)" %
                  (name, os.path.basename(target), want,
                   describe(read_fourcc(target))))
            return 2

        cur_kind = FOURCC_TO_KIND.get(cur_fourcc)
        if os.path.exists(active) and cur_kind is None:
            print("  %-16s [ERROR] 現在の .avi の種別が判別できません (%s)。"
                  "手作業で確認してください。" % (name, describe(cur_fourcc)))
            return 2
        planned.append((name, active, target, cur_kind))

    # すべて問題ないと分かってから動かす
    for name, active, target, cur_kind in planned:
        if cur_kind is not None:
            os.replace(active, active + "." + cur_kind)
        os.replace(target, active)
        print("  %-16s %s -> %s" % (name, cur_kind or "(なし)", want))
    return 0


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument("kind", nargs="?", choices=sorted(KINDS),
                    help="切り替え先。省略すると状態表示のみ。")
    ap.add_argument("--movdir", default=os.path.join(os.path.dirname(here), "mov"))
    a = ap.parse_args()

    if not os.path.isdir(a.movdir):
        print("[ERROR] mov フォルダが見つかりません: %s" % a.movdir)
        return 1

    if a.kind is None:
        status(a.movdir)
        print()
        print("切り替え: python switch_movies.py {%s}" % "|".join(sorted(KINDS)))
        return 0

    print("%s (%s) に切り替えます" % (a.kind, KINDS[a.kind][1]))
    rc = switch(a.movdir, a.kind)
    print()
    status(a.movdir)
    return rc


if __name__ == "__main__":
    sys.exit(main())
