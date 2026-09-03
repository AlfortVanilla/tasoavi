#!/usr/bin/env python3
"""
誰彼 -たそがれ- の mov\\*.avi を Indeo 5 から YUY2(無圧縮) に変換します。

  python convert_movies.py --ffmpeg <ffmpeg.exe のパス> [--movdir <movフォルダ>]
                           [--outdir <出力先>] [--dry-run]

なぜ YUY2 か
------------
ゲームは画面の色深度 (GetDeviceCaps(BITSPIXEL)) をそのままデコード要求の
ビット深度に使います。Windows 11 のデスクトップは 32bpp なので、32bpp 要求で
正しく動く形式でなければいけません。実測:

    要求 32bpp のときの原版との PSNR
      YUY2 / UYVY   36.6 dB   <- 最良
      Cinepak       31.9 dB
      I420          2.9 dB    <- 画が壊れる (iyuv_32.dll が 32bpp で誤動作)
      YVU9          開けない
      無圧縮 RGB24  開けない  (ffmpeg が biHeight を負で書くため)

YUY2 のデコーダ msyuv.dll は Windows 11 に標準搭載され、Microsoft が署名・
保守しています。Indeo (署名なし・2000年で更新停止) を外すのが目的なので、
これで依存が無くなります。

なぜ自前で AVI を書くのか
-------------------------
ffmpeg の AVI マルチプレクサは 1GB ごとに RIFF を分割し OpenDML AVI を作ります。
Windows の AVIFile は分割境界より後ろのフレームを読めません (実測: frame 2000
は取得できるが 2400 以降は失敗)。Tasogare640_30 は YUY2 で 1.65GB になるため、
ここでは ffmpeg には生のフレームだけ出させ、単一 RIFF の AVI をこちらで書きます。
1.65GB は 2GB のレガシー AVI 上限に収まります。

音声ストリームは付けません (ゲームは音声つき AVI を弾きます。原版も映像のみ)。
"""
import argparse
import os
import struct
import subprocess
import sys

AVIF_HASINDEX   = 0x00000010
AVIIF_KEYFRAME  = 0x00000010
FOURCC          = b"YUY2"
BITCOUNT        = 16
BYTES_PER_PIXEL = 2
AVI_LIMIT       = 2 * 1024 * 1024 * 1024      # レガシー AVI の上限


def read_source_header(path):
    """原版 AVI から寸法・フレームレート・フレーム数を読む。"""
    with open(path, "rb") as f:
        d = f.read(0x1000)
    if d[:4] != b"RIFF" or d[8:12] != b"AVI ":
        raise ValueError("%s: AVI ではありません" % path)

    i = d.index(b"avih")
    usec_per_frame, _maxbps, _pad, _flags, total, _init, streams, _buf, w, h = \
        struct.unpack_from("<10I", d, i + 8)

    j = d.index(b"strh")
    if d[j + 8:j + 12] != b"vids":
        raise ValueError("%s: 先頭ストリームが映像ではありません" % path)
    # 'strh'(4) + cb(4) + fccType(4) + fccHandler(4) + dwFlags(4)
    # + wPriority(2) + wLanguage(2) + dwInitialFrames(4) = +28 から dwScale
    scale, rate, _start, length = struct.unpack_from("<4I", d, j + 28)

    return dict(width=w, height=h, scale=scale, rate=rate,
                frames=length or total, usec=usec_per_frame, streams=streams)


def build_headers(info, frame_bytes, total_frames):
    w, h = info["width"], info["height"]

    avih = struct.pack(
        "<16I",
        info["usec"],                                   # dwMicroSecPerFrame
        frame_bytes * info["rate"] // max(info["scale"], 1),  # dwMaxBytesPerSec
        0,                                              # dwPaddingGranularity
        AVIF_HASINDEX,                                  # dwFlags
        total_frames,                                   # dwTotalFrames
        0,                                              # dwInitialFrames
        1,                                              # dwStreams
        frame_bytes,                                    # dwSuggestedBufferSize
        w, h,
        0, 0, 0, 0, 0, 0)[:56]

    strh = (b"vids" + FOURCC +
            struct.pack("<IHHIIIIIIiI4h",
                        0,                  # dwFlags
                        0, 0,               # wPriority, wLanguage
                        0,                  # dwInitialFrames
                        info["scale"],
                        info["rate"],
                        0,                  # dwStart
                        total_frames,       # dwLength
                        frame_bytes,        # dwSuggestedBufferSize
                        -1,                 # dwQuality
                        0,                  # dwSampleSize
                        0, 0, w, h))        # rcFrame

    strf = struct.pack("<IiiHH4sIiiII",
                       40, w, h, 1, BITCOUNT, FOURCC, frame_bytes, 0, 0, 0, 0)

    assert len(avih) == 56 and len(strh) == 56 and len(strf) == 40, \
        (len(avih), len(strh), len(strf))
    return avih, strh, strf


def convert(ffmpeg, src, dst):
    info = read_source_header(src)
    w, h = info["width"], info["height"]
    frame_bytes = w * h * BYTES_PER_PIXEL
    expected = info["frames"]

    est = frame_bytes * expected
    print("  %dx%d  %d frames  %d/%d fps  ->  約 %.2f GiB" %
          (w, h, expected, info["rate"], info["scale"], est / (1 << 30)))
    if est + (1 << 20) >= AVI_LIMIT:
        raise ValueError("出力が 2GB を超えます。この形式では変換できません。")

    avih, strh, strf = build_headers(info, frame_bytes, expected)

    proc = subprocess.Popen(
        [ffmpeg, "-hide_banner", "-loglevel", "error", "-i", src,
         "-an", "-f", "rawvideo", "-pix_fmt", "yuyv422", "-"],
        stdout=subprocess.PIPE, bufsize=1 << 22)

    index = []
    with open(dst, "wb") as out:
        out.write(b"RIFF" + struct.pack("<I", 0) + b"AVI ")

        hdrl = (b"hdrl" +
                b"avih" + struct.pack("<I", len(avih)) + avih +
                b"LIST" + struct.pack("<I", 4 + 8 + len(strh) + 8 + len(strf)) +
                b"strl" +
                b"strh" + struct.pack("<I", len(strh)) + strh +
                b"strf" + struct.pack("<I", len(strf)) + strf)
        out.write(b"LIST" + struct.pack("<I", len(hdrl)) + hdrl)

        movi_list_pos = out.tell()
        out.write(b"LIST" + struct.pack("<I", 0) + b"movi")
        movi_data_pos = movi_list_pos + 8          # 'movi' の位置

        n = 0
        while True:
            buf = proc.stdout.read(frame_bytes)
            if not buf:
                break
            if len(buf) != frame_bytes:
                raise ValueError("フレーム %d が途中で切れています" % n)
            index.append((out.tell() - movi_data_pos, frame_bytes))
            out.write(b"00dc" + struct.pack("<I", frame_bytes) + buf)
            n += 1
            if n % 500 == 0:
                print("    %d / %d frames" % (n, expected), end="\r", flush=True)
        proc.stdout.close()
        if proc.wait() != 0:
            raise ValueError("ffmpeg が異常終了しました")

        movi_end = out.tell()
        out.write(b"idx1" + struct.pack("<I", 16 * n))
        for off, size in index:
            out.write(b"00dc" + struct.pack("<III", AVIIF_KEYFRAME, off, size))
        file_end = out.tell()

        # 実フレーム数でヘッダを直す
        if n != expected:
            print("    ! フレーム数が %d (ヘッダ想定 %d) だったので補正します" % (n, expected))
            avih, strh, strf = build_headers(info, frame_bytes, n)
            out.seek(12 + 8 + 4 + 8); out.write(avih)
            out.seek(12 + 8 + 4 + 8 + len(avih) + 8 + 4 + 8); out.write(strh)

        out.seek(4);  out.write(struct.pack("<I", file_end - 8))
        out.seek(movi_list_pos + 4); out.write(struct.pack("<I", movi_end - movi_data_pos))

    print("    完了: %d frames, %d bytes (%.2f GiB)" % (n, file_end, file_end / (1 << 30)))
    return n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ffmpeg", required=True)
    ap.add_argument("--movdir", default=os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "mov"))
    ap.add_argument("--outdir", default=None)
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    outdir = a.outdir or a.movdir
    os.makedirs(outdir, exist_ok=True)

    names = ["Title320_15", "Title640_30", "Tasogare320_15", "Tasogare640_30"]
    for name in names:
        src = os.path.join(a.movdir, name + ".avi")
        if not os.path.exists(src):
            print("%s : 見つかりません。スキップします。" % name)
            continue
        dst = os.path.join(outdir, name + ".yuy2.avi")
        print("%s" % name)
        if a.dry_run:
            info = read_source_header(src)
            print("  %dx%d %d frames -> 約 %.2f GiB" % (
                info["width"], info["height"], info["frames"],
                info["width"] * info["height"] * 2 * info["frames"] / (1 << 30)))
            continue
        convert(a.ffmpeg, src, dst)
    return 0


if __name__ == "__main__":
    sys.exit(main())
