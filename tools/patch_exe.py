#!/usr/bin/env python3
"""
誰彼 -たそがれ-.exe のインポートテーブルの "AVIFIL32.dll" を "tasoavi.dll" に
書き換えます。ゲームは以後 AVIFile API を tasoavi.dll 経由で呼ぶようになります。

  python patch_exe.py "誰彼 -たそがれ-.exe"          パッチ適用 (.bak を自動作成)
  python patch_exe.py "誰彼 -たそがれ-.exe" --revert  元に戻す
  python patch_exe.py "誰彼 -たそがれ-.exe" --check   現在の状態を表示するだけ

インポート記述子を実際に解析して該当箇所だけを書き換えるので、
たまたま同じ文字列が他にあっても影響しません。
"""
import struct
import sys
import os
import shutil

ORIGINAL = b"AVIFIL32.dll"
SHIM     = b"tasoavi.dll"

# 出荷時の exe が DLL 名の文字列に使っている領域 ("AVIFIL32.dll" + 終端 NUL)。
# 書き換えは常にこの範囲だけを対象にし、余りは NUL で埋める。
FIELD = len(ORIGINAL) + 1


def parse_sections(data):
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if data[e_lfanew:e_lfanew + 4] != b"PE\0\0":
        raise ValueError("PE ファイルではありません")
    coff = e_lfanew + 4
    nsec, = struct.unpack_from("<H", data, coff + 2)
    optsz, = struct.unpack_from("<H", data, coff + 16)
    opt = coff + 20
    magic, = struct.unpack_from("<H", data, opt)
    if magic != 0x10B:
        raise ValueError("32bit の PE ではありません")
    imp_rva, imp_size = struct.unpack_from("<II", data, opt + 96 + 8)

    sect = opt + optsz
    secs = []
    for i in range(nsec):
        o = sect + 40 * i
        vsz, va, rsz, ro = struct.unpack_from("<IIII", data, o + 8)
        secs.append((vsz, va, rsz, ro))
    return secs, imp_rva


def rva_to_offset(secs, rva):
    for vsz, va, rsz, ro in secs:
        if va <= rva < va + max(vsz, rsz):
            return ro + (rva - va)
    raise ValueError("RVA 0x%08X をファイル内に解決できません" % rva)


def find_import_name(data, want):
    """インポート記述子を走査し、DLL 名 want の文字列のファイル内位置を返す。"""
    secs, imp_rva = parse_sections(data)
    off = rva_to_offset(secs, imp_rva)
    i = 0
    found = []
    while True:
        entry = data[off + 20 * i: off + 20 * i + 20]
        if len(entry) < 20 or entry == b"\0" * 20:
            break
        name_rva = struct.unpack_from("<I", entry, 12)[0]
        no = rva_to_offset(secs, name_rva)
        end = data.index(b"\0", no)
        name = data[no:end]
        if name.lower() == want.lower():
            found.append((no, name))
        i += 1
    return found


def show(data, path):
    secs, imp_rva = parse_sections(data)
    off = rva_to_offset(secs, imp_rva)
    print("%s のインポート DLL:" % os.path.basename(path))
    i = 0
    while True:
        entry = data[off + 20 * i: off + 20 * i + 20]
        if len(entry) < 20 or entry == b"\0" * 20:
            break
        name_rva = struct.unpack_from("<I", entry, 12)[0]
        no = rva_to_offset(secs, name_rva)
        name = data[no:data.index(b"\0", no)].decode("latin1")
        mark = ""
        if name.lower() == ORIGINAL.decode().lower():
            mark = "   <- 未パッチ"
        elif name.lower() == SHIM.decode().lower():
            mark = "   <- パッチ済み"
        print("    %s%s" % (name, mark))
        i += 1


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    path = sys.argv[1]
    mode = sys.argv[2] if len(sys.argv) > 2 else "--patch"

    data = bytearray(open(path, "rb").read())

    if mode == "--check":
        show(data, path)
        return 0

    src, dst = (SHIM, ORIGINAL) if mode == "--revert" else (ORIGINAL, SHIM)

    hits = find_import_name(data, src)
    if not hits:
        already = find_import_name(data, dst)
        if already:
            print("すでに %s を参照しています。何もしません。" % dst.decode())
            return 0
        print("[ERROR] インポートテーブルに %s が見つかりません。" % src.decode())
        return 2
    if len(hits) > 1:
        print("[ERROR] %s のインポートが複数あります。中止します。" % src.decode())
        return 2

    off, name = hits[0]
    if len(dst) >= FIELD:
        print("[ERROR] 置換後の名前が長すぎます (最大 %d 文字)。" % (FIELD - 1))
        return 2

    backup = path + ".bak"
    if not os.path.exists(backup):
        shutil.copy2(path, backup)
        print("バックアップを作成: %s" % os.path.basename(backup))

    # 出荷時の文字列領域ぴったりに書き込む。残りは NUL 埋めなので終端は必ず残る。
    data[off:off + FIELD] = dst + b"\0" * (FIELD - len(dst))
    open(path, "wb").write(bytes(data))

    print("%s -> %s に書き換えました。" % (src.decode(), dst.decode()))
    print()
    show(bytearray(open(path, "rb").read()), path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
