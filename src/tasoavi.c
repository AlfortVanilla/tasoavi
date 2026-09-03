/*
 *  tasoavi.dll  --  AVIFile compatibility shim for
 *                   "誰彼 -たそがれ-" (Leaf / AQUAPLUS, 2001)
 *
 *  ---------------------------------------------------------------------
 *  なぜ Windows 11 でムービーが再生できないのか
 *  ---------------------------------------------------------------------
 *  ゲーム本体は AVIStreamGetFrameOpen() に「トップダウン DIB」
 *  (BITMAPINFOHEADER.biHeight が負) を要求する。
 *  Windows 9x/2000/XP の avifil32.dll はこれを受け付けたが、
 *  Windows 10/11 の avifil32.dll は biHeight が負というだけで NULL を返す。
 *  コーデックの種類とは無関係で、Indeo 5 でも Cinepak でも無圧縮でも同じ。
 *
 *  ゲームはもう一度ストリーム本来の形式で開き直そうとするが、そちらも
 *  トップダウン要求のままなので再び失敗し、
 *      「AVIが描画できません。Codecがインストールされていないと思われます。」
 *  を表示して停止する。
 *
 *  ---------------------------------------------------------------------
 *  このDLLがやること
 *  ---------------------------------------------------------------------
 *  ゲームが使う 11 本の AVIFile API を中継する。8 本はそのまま素通し。
 *  フレーム取得まわりの 3 本だけ次のように振る舞う:
 *
 *    GetFrameOpen  : biHeight が負なら正(ボトムアップ)に直して OS に渡す
 *    GetFrame      : 返ってきたボトムアップ画像の行順を反転し、
 *                    biHeight を負に戻したトップダウン画像として返す
 *    GetFrameClose : 上記で確保した領域を解放する
 *
 *  結果としてゲームは「要求どおりのトップダウン DIB」を受け取る。
 *  ゲーム本体の描画コードには一切手を入れない。
 */

#include <windows.h>
#include <vfw.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================== */
/*  フレーム取得ハンドルのラッパ                                        */
/* ================================================================== */

#define WRAP_MAGIC 0x31565441u          /* ATV1 */

typedef struct {
    DWORD     magic;
    PGETFRAME real;                     /* OS が返した本物のハンドル */
    BOOL      flip;                     /* 行順を反転して返すか      */
    BYTE     *buf;                      /* 反転後のパック DIB        */
    size_t    bufsz;
} FRAMEWRAP;

/*  扱う DIB の上限。ゲームが要求する最大は 640x352x4 = 約 0.9MB なので
    これで十分に余裕がある。異常な値を弾くための天井である。            */
#define MAX_DIB_BYTES  (64u * 1024u * 1024u)
#define MAX_DIB_DIM    65535L

/* DIB の 1 行あたりのバイト数 (4バイト境界に切り上げ)。
   桁あふれしない範囲でのみ成功する。                                    */
static BOOL DibStrideChecked(LONG width, WORD bitCount, DWORD *out)
{
    DWORD bits;

    if (width <= 0 || width > MAX_DIB_DIM)   return FALSE;
    if (bitCount == 0 || bitCount > 32)      return FALSE;

    bits = (DWORD)width * (DWORD)bitCount;   /* 最大 65535*32、桁あふれしない */
    *out = ((bits + 31u) / 32u) * 4u;        /* 最大 262144                   */
    return TRUE;
}

/* BITMAPINFOHEADER の先頭から画素データ先頭までのバイト数。
   biSize / biClrUsed はデコーダが返した値なので上限を設ける。            */
static BOOL DibHeadroomChecked(const BITMAPINFOHEADER *bi, DWORD *out)
{
    DWORD entries, extra;

    if (bi->biSize < sizeof(BITMAPINFOHEADER) || bi->biSize > 4096u)
        return FALSE;

    if (bi->biBitCount <= 8) {
        entries = bi->biClrUsed ? bi->biClrUsed : (1u << bi->biBitCount);
        if (entries > 256u) return FALSE;
        extra = entries * 4u;
    } else if (bi->biCompression == BI_BITFIELDS) {
        extra = 12u;
    } else {
        if (bi->biClrUsed > 256u) return FALSE;
        extra = bi->biClrUsed * 4u;
    }

    *out = bi->biSize + extra;                /* 最大 4096+1024 */
    return TRUE;
}

/* ================================================================== */
/*  中身を差し替える 3 本                                               */
/* ================================================================== */

PGETFRAME WINAPI Shim_AVIStreamGetFrameOpen(PAVISTREAM pavi, LPBITMAPINFOHEADER lpbi)
{
    BITMAPINFOHEADER    bottomUp;
    LPBITMAPINFOHEADER  request = lpbi;
    BOOL                flip    = FALSE;
    PGETFRAME           real;
    FRAMEWRAP          *wrap;

    /* lpbi は NULL、あるいは AVIGETFRAMEF_BESTDISPLAYFMT (=1) という
       小さな定数のこともある。本物のポインタのときだけ中身を見る。 */
    if ((ULONG_PTR)lpbi > 0xFFFF &&
        lpbi->biSize >= sizeof(BITMAPINFOHEADER) &&
        lpbi->biHeight < 0 && lpbi->biHeight > -MAX_DIB_DIM)
    {
        DWORD stride;
        memcpy(&bottomUp, lpbi, sizeof(bottomUp));
        bottomUp.biSize   = sizeof(BITMAPINFOHEADER);
        bottomUp.biHeight = -lpbi->biHeight;
        if (bottomUp.biSizeImage != 0 &&    /* 0 のときは 0 のまま渡す */
            DibStrideChecked(bottomUp.biWidth, bottomUp.biBitCount, &stride))
            bottomUp.biSizeImage = stride * (DWORD)bottomUp.biHeight;
        request = &bottomUp;
        flip    = TRUE;
    }

    real = AVIStreamGetFrameOpen(pavi, request);

    if (real == NULL && flip) {          /* 念のため元の要求でも試す */
        real = AVIStreamGetFrameOpen(pavi, lpbi);
        flip = FALSE;
    }
    if (real == NULL)
        return NULL;

    wrap = (FRAMEWRAP *)calloc(1, sizeof(FRAMEWRAP));
    if (wrap == NULL) {
        AVIStreamGetFrameClose(real);
        return NULL;
    }
    wrap->magic = WRAP_MAGIC;
    wrap->real  = real;
    wrap->flip  = flip;
    return (PGETFRAME)wrap;
}

LPVOID WINAPI Shim_AVIStreamGetFrame(PGETFRAME pgf, LONG lPos)
{
    FRAMEWRAP        *wrap = (FRAMEWRAP *)pgf;
    BITMAPINFOHEADER *src;
    BITMAPINFOHEADER *dst;
    DWORD             headroom, stride;
    LONG              height, y;
    const BYTE       *srcBits;
    BYTE             *dstBits;
    size_t            need;

    if (wrap == NULL || wrap->magic != WRAP_MAGIC)
        return AVIStreamGetFrame(pgf, lPos);        /* うちの物ではない */

    src = (BITMAPINFOHEADER *)AVIStreamGetFrame(wrap->real, lPos);
    if (src == NULL || !wrap->flip)
        return src;                                 /* 反転不要 */

    /* 行を入れ替えてよいのは非圧縮 RGB のときだけ。YUV の DIB は
       biHeight の符号によらず常にトップダウンと規定されている。      */
    if (src->biCompression != BI_RGB && src->biCompression != BI_BITFIELDS)
        return src;

    height = src->biHeight;
    if (height <= 0 || height > MAX_DIB_DIM)
        return src;                                 /* 既にトップダウン / 不正 */

    if (!DibStrideChecked(src->biWidth, src->biBitCount, &stride) ||
        !DibHeadroomChecked(src, &headroom))
        return src;                                 /* 解釈できない DIB は素通し */

    /* stride * height は 32bit に収まらないことがあるので 64bit で判定する。 */
    if ((ULONGLONG)stride * (ULONGLONG)height + headroom > MAX_DIB_BYTES)
        return src;

    need = (size_t)(headroom + stride * (DWORD)height);

    if (wrap->bufsz < need) {
        BYTE *grown = (BYTE *)realloc(wrap->buf, need);
        if (grown == NULL)
            return src;                             /* 確保できなければ素通し */
        wrap->buf   = grown;
        wrap->bufsz = need;
    }

    memcpy(wrap->buf, src, headroom);
    dst = (BITMAPINFOHEADER *)wrap->buf;
    dst->biHeight    = -height;                     /* トップダウンとして返す */
    dst->biSizeImage = stride * (DWORD)height;

    srcBits = (const BYTE *)src + headroom;
    dstBits = wrap->buf + headroom;
    for (y = 0; y < height; ++y)                    /* 行順を反転 */
        memcpy(dstBits + (size_t)y * stride,
               srcBits + (size_t)(height - 1 - y) * stride,
               stride);

    return wrap->buf;
}

HRESULT WINAPI Shim_AVIStreamGetFrameClose(PGETFRAME pgf)
{
    FRAMEWRAP *wrap = (FRAMEWRAP *)pgf;
    HRESULT    hr;

    if (wrap == NULL)
        return AVIERR_OK;
    if (wrap->magic != WRAP_MAGIC)
        return AVIStreamGetFrameClose(pgf);

    hr = AVIStreamGetFrameClose(wrap->real);
    free(wrap->buf);
    wrap->magic = 0;
    free(wrap);
    return hr;
}

/* ================================================================== */
/*  そのまま OS に渡すだけの分                                          */
/*                                                                    */
/*  ゲームが実際に使うのは Init/Exit/OpenA/InfoA/GetStream/Release/    */
/*  StreamInfoA/StreamReadFormat の 8 本だけだが、AVIFile を読むだけの  */
/*  クライアントが普通に使う関数はひととおり通しておく。                 */
/* ================================================================== */

void WINAPI Shim_AVIFileInit(void)
{
    AVIFileInit();
}

void WINAPI Shim_AVIFileExit(void)
{
    AVIFileExit();
}

HRESULT WINAPI Shim_AVIFileOpenA(PAVIFILE *ppfile, LPCSTR szFile, UINT uMode, LPCLSID lpHandler)
{
    return AVIFileOpenA(ppfile, szFile, uMode, lpHandler);
}

HRESULT WINAPI Shim_AVIFileInfoA(PAVIFILE pfile, LPAVIFILEINFOA pfi, LONG lSize)
{
    return AVIFileInfoA(pfile, pfi, lSize);
}

HRESULT WINAPI Shim_AVIFileGetStream(PAVIFILE pfile, PAVISTREAM *ppavi, DWORD fccType, LONG lParam)
{
    return AVIFileGetStream(pfile, ppavi, fccType, lParam);
}

ULONG WINAPI Shim_AVIFileRelease(PAVIFILE pfile)
{
    return AVIFileRelease(pfile);
}

HRESULT WINAPI Shim_AVIStreamInfoA(PAVISTREAM pavi, LPAVISTREAMINFOA psi, LONG lSize)
{
    return AVIStreamInfoA(pavi, psi, lSize);
}

HRESULT WINAPI Shim_AVIStreamReadFormat(PAVISTREAM pavi, LONG lPos, LPVOID lpFormat, LONG *lpcbFormat)
{
    return AVIStreamReadFormat(pavi, lPos, lpFormat, lpcbFormat);
}

ULONG WINAPI Shim_AVIFileAddRef(PAVIFILE pfile)
{
    return AVIFileAddRef(pfile);
}

ULONG WINAPI Shim_AVIStreamAddRef(PAVISTREAM pavi)
{
    return AVIStreamAddRef(pavi);
}

ULONG WINAPI Shim_AVIStreamRelease(PAVISTREAM pavi)
{
    return AVIStreamRelease(pavi);
}

HRESULT WINAPI Shim_AVIStreamOpenFromFileA(PAVISTREAM *ppavi, LPCSTR szFile, DWORD fccType,
                                           LONG lParam, UINT mode, CLSID *pclsidHandler)
{
    return AVIStreamOpenFromFileA(ppavi, szFile, fccType, lParam, mode, pclsidHandler);
}

HRESULT WINAPI Shim_AVIStreamRead(PAVISTREAM pavi, LONG lStart, LONG lSamples, LPVOID lpBuffer,
                                  LONG cbBuffer, LONG *plBytes, LONG *plSamples)
{
    return AVIStreamRead(pavi, lStart, lSamples, lpBuffer, cbBuffer, plBytes, plSamples);
}

LONG WINAPI Shim_AVIStreamLength(PAVISTREAM pavi)
{
    return AVIStreamLength(pavi);
}

LONG WINAPI Shim_AVIStreamStart(PAVISTREAM pavi)
{
    return AVIStreamStart(pavi);
}

LONG WINAPI Shim_AVIStreamFindSample(PAVISTREAM pavi, LONG lPos, LONG lFlags)
{
    return AVIStreamFindSample(pavi, lPos, lFlags);
}

LONG WINAPI Shim_AVIStreamSampleToTime(PAVISTREAM pavi, LONG lSample)
{
    return AVIStreamSampleToTime(pavi, lSample);
}

LONG WINAPI Shim_AVIStreamTimeToSample(PAVISTREAM pavi, LONG lTime)
{
    return AVIStreamTimeToSample(pavi, lTime);
}
