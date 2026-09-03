#!/usr/bin/env python3
"""
配布用の zip を組み立てます。開発用のツールなので、これ自体は同梱しません。

  python make_release.py                1.0.0 で組み立てる
  python make_release.py --version 1.1.0
  python make_release.py --outdir D:\\somewhere

やること:
  1. release/tasoavi-<version>-win32/ に必要なファイルだけを配置する
  2. SHA256SUMS.txt を生成する
  3. zip に固める

ゲーム本体の著作物 (exe, *.pak, mov/*.avi) は絶対に入りません。同梱するのは
このフォルダの成果物とソースだけです。

indeo_backup/ __pycache__/ release/ といった作業用のものは、
FILES に挙げたものだけを明示的にコピーする作りなので自動的に外れます。
"""
import argparse
import hashlib
import os
import shutil
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))

# 配布するファイル。リポジトリ上の配置がそのまま配布物の配置になる。
FILES = [
    "README.md",
    "CHANGELOG.md",
    "LICENSE.txt",
    "tasoavi.dll",
    "tasopatch.exe",
    "tools/patch_exe.py",
    "tools/convert_movies.py",
    "tools/switch_movies.py",
    "tools/remove_indeo.bat",
    "src/tasoavi.c",
    "src/tasoavi.def",
    "src/tasoavi.rc",
    "src/tasopatch.c",
    "src/tasopatch.rc",
    "src/tasopatch.manifest",
    "src/build.bat",
    "docs/TECHNICAL.md",
]

BINARIES = ["tasoavi.dll", "tasopatch.exe"]
SOURCES  = ["src/tasoavi.c", "src/tasoavi.def", "src/tasoavi.rc",
            "src/tasopatch.c", "src/tasopatch.rc", "src/tasopatch.manifest"]


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def check_freshness():
    """ソースより古いバイナリが混じっていないか確かめる。"""
    stale = []
    newest_src = max(os.path.getmtime(os.path.join(HERE, s))
                     for s in SOURCES if os.path.exists(os.path.join(HERE, s)))
    for b in BINARIES:
        p = os.path.join(HERE, b)
        if not os.path.exists(p):
            return ["%s がありません。build.bat を実行してください。" % b]
        if os.path.getmtime(p) < newest_src:
            stale.append("%s がソースより古いです。build.bat を実行し直してください。" % b)
    return stale


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", default="1.0.0")
    ap.add_argument("--outdir", default=os.path.join(HERE, "release"))
    ap.add_argument("--force", action="store_true",
                    help="バイナリが古くても続行する")
    args = ap.parse_args()

    problems = check_freshness()
    if problems:
        for p in problems:
            print("[WARN] %s" % p)
        if not args.force:
            print("\n中止しました。--force で無視できます。")
            return 1

    name = "tasoavi-%s-win32" % args.version
    stage = os.path.join(args.outdir, name)
    if os.path.exists(stage):
        shutil.rmtree(stage)

    missing = []
    for rel in FILES:
        s = os.path.join(HERE, rel.replace("/", os.sep))
        if not os.path.exists(s):
            missing.append(rel)
            continue
        d = os.path.join(stage, rel.replace("/", os.sep))
        os.makedirs(os.path.dirname(d), exist_ok=True)
        shutil.copy2(s, d)

    if missing:
        print("[ERROR] 見つからないファイル:")
        for m in missing:
            print("    %s" % m)
        if "LICENSE.txt" in missing:
            print("\n  LICENSE.txt はまだ作られていません。ライセンスを決めて")
            print("  このフォルダに置いてから、もう一度実行してください。")
        return 2

    # SHA256SUMS.txt (自分自身は含めない)
    lines = []
    for root, _, files in os.walk(stage):
        for fn in sorted(files):
            full = os.path.join(root, fn)
            rel = os.path.relpath(full, stage).replace(os.sep, "/")
            lines.append("%s *%s" % (sha256(full), rel))
    lines.sort(key=lambda l: l.split("*", 1)[1])
    with open(os.path.join(stage, "SHA256SUMS.txt"), "w",
              encoding="utf-8", newline="\r\n") as f:
        f.write("\n".join(lines) + "\n")

    zip_path = os.path.join(args.outdir, name + ".zip")
    if os.path.exists(zip_path):
        os.remove(zip_path)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for root, _, files in os.walk(stage):
            for fn in sorted(files):
                full = os.path.join(root, fn)
                arc = os.path.join(name, os.path.relpath(full, stage))
                z.write(full, arc.replace(os.sep, "/"))

    print("=== %s ===" % os.path.basename(zip_path))
    print(zip_path)
    print()
    for b in BINARIES:
        print("  %s  %s" % (sha256(os.path.join(stage, b)), b))
    print()
    print("  この2つのハッシュを README.md に書き、VirusTotal に上げてください。")
    print("  再ビルドすると必ず変わります (MSVC がビルド時刻を埋めるため)。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
