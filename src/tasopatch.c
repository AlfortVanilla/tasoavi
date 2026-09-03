/*
 *  tasopatch.exe  --  誰彼 -たそがれ-.exe のインポート名を書き換える GUI ツール
 *
 *  patch_exe.py と同じことを、Python 無しでできるようにしたもの。
 *  PE のインポート記述子を実際に解析し、DLL 名文字列の該当箇所だけを
 *  書き換える。たまたま同じ文字列が他にあっても影響しない。
 *
 *      AVIFIL32.dll  ->  tasoavi.dll     (パッチ適用)
 *      tasoavi.dll   ->  AVIFIL32.dll    (元に戻す)
 *
 *  書き換えは出荷時の "AVIFIL32.dll\0" の 13 バイトぴったりの中で行い、
 *  余りは NUL で埋める。ファイル長も他の構造も一切変わらない。
 *
 *  対象の指定は3通り:
 *      1. ウィンドウにドラッグ&ドロップ (exe でもフォルダでも可)
 *      2. [参照...] ボタン
 *      3. 起動時の自動検出 (このexeと同じフォルダ、およびその親)
 *  exe のアイコンにファイルを落とした場合はコマンドライン経由で受け取る。
 */

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <strsafe.h>

/* ------------------------------------------------------------------ */
/*  定数                                                               */
/* ------------------------------------------------------------------ */

static const char  kOrig[] = "AVIFIL32.dll";
static const char  kShim[] = "tasoavi.dll";

/* 出荷時の exe が DLL 名に使っている領域 ("AVIFIL32.dll" + 終端 NUL) */
#define NAME_FIELD   ((DWORD)sizeof(kOrig))      /* = 13 */

#define GAME_EXE     L"誰彼 -たそがれ-.exe"
#define APP_TITLE    L"tasoavi パッチャ"

#define MAX_FILE_MB  64u
#define MAX_IMPORTS  64
#define INFO_CHARS   4096

enum { ST_NONE = 0, ST_UNPATCHED, ST_PATCHED, ST_BAD };

enum { ID_DROP = 1001, ID_INFO, ID_PATCH, ID_REVERT, ID_BROWSE, ID_CLOSE };

/* ------------------------------------------------------------------ */
/*  PE 解析                                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    BYTE                       *data;
    DWORD                       size;
    const IMAGE_SECTION_HEADER *secs;
    WORD                        nsec;
    DWORD                       impRva;
} PEIMG;

typedef struct {
    char  name[64];
    DWORD nameOff;          /* DLL 名文字列のファイル内位置 */
} IMPENT;

/* ファイルを丸ごと読む。呼び出し側が HeapFree すること。 */
static BOOL ReadWholeFile(const WCHAR *path, BYTE **out, DWORD *outSize)
{
    HANDLE h;
    DWORD  size, got;
    BYTE  *buf;

    *out = NULL;
    *outSize = 0;

    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return FALSE;

    size = GetFileSize(h, NULL);
    if (size == INVALID_FILE_SIZE || size < 0x40 || size > MAX_FILE_MB * 1024u * 1024u) {
        CloseHandle(h);
        return FALSE;
    }

    buf = (BYTE *)HeapAlloc(GetProcessHeap(), 0, size);
    if (buf == NULL) {
        CloseHandle(h);
        return FALSE;
    }
    if (!ReadFile(h, buf, size, &got, NULL) || got != size) {
        HeapFree(GetProcessHeap(), 0, buf);
        CloseHandle(h);
        return FALSE;
    }
    CloseHandle(h);

    *out = buf;
    *outSize = size;
    return TRUE;
}

static BOOL PeParse(BYTE *data, DWORD size, PEIMG *pe)
{
    const IMAGE_DOS_HEADER *dos;
    DWORD lfanew, coff, opt, sect;
    WORD  nsec, optsz, magic;

    ZeroMemory(pe, sizeof(*pe));
    pe->data = data;
    pe->size = size;

    dos = (const IMAGE_DOS_HEADER *)data;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return FALSE;

    lfanew = (DWORD)dos->e_lfanew;
    if (lfanew > size || size - lfanew < 4 + 20)
        return FALSE;
    if (*(const DWORD UNALIGNED *)(data + lfanew) != IMAGE_NT_SIGNATURE)
        return FALSE;

    coff  = lfanew + 4;
    nsec  = *(const WORD UNALIGNED *)(data + coff + 2);
    optsz = *(const WORD UNALIGNED *)(data + coff + 16);
    opt   = coff + 20;

    /* DataDirectory[1] (インポート) まで届く大きさが要る */
    if (opt > size || size - opt < optsz || optsz < 96 + 16)
        return FALSE;

    magic = *(const WORD UNALIGNED *)(data + opt);
    if (magic != 0x10B)                 /* PE32 (32bit) のみ扱う */
        return FALSE;

    pe->impRva = *(const DWORD UNALIGNED *)(data + opt + 96 + 8);

    sect = opt + optsz;
    if (sect > size ||
        (size - sect) / sizeof(IMAGE_SECTION_HEADER) < nsec)
        return FALSE;

    pe->secs = (const IMAGE_SECTION_HEADER *)(data + sect);
    pe->nsec = nsec;
    return TRUE;
}

static BOOL RvaToOff(const PEIMG *pe, DWORD rva, DWORD *off)
{
    WORD i;

    for (i = 0; i < pe->nsec; ++i) {
        DWORD va   = pe->secs[i].VirtualAddress;
        DWORD vsz  = pe->secs[i].Misc.VirtualSize;
        DWORD rsz  = pe->secs[i].SizeOfRawData;
        DWORD ptr  = pe->secs[i].PointerToRawData;
        DWORD span = (vsz > rsz) ? vsz : rsz;
        DWORD d;

        if (rva < va || rva - va >= span)
            continue;
        d = rva - va;
        if (ptr > pe->size || d > pe->size - ptr)   /* 桁あふれなしで範囲判定 */
            return FALSE;
        *off = ptr + d;
        return TRUE;
    }
    return FALSE;
}

/* off から NUL 終端の ASCII 名を読む。ファイル外に出たら FALSE。 */
static BOOL ReadName(const PEIMG *pe, DWORD off, char *buf, DWORD cap)
{
    DWORD i;

    for (i = 0; off + i < pe->size && i + 1 < cap; ++i) {
        buf[i] = (char)pe->data[off + i];
        if (buf[i] == '\0')
            return TRUE;
    }
    return FALSE;
}

/* インポート記述子を走査して DLL 名を集める。返り値は件数。 */
static int PeImports(const PEIMG *pe, IMPENT *out, int cap)
{
    DWORD off, i;
    int   n = 0;

    if (pe->impRva == 0 || !RvaToOff(pe, pe->impRva, &off))
        return 0;

    for (i = 0; i < 256 && n < cap; ++i) {
        DWORD e = off + 20 * i;
        DWORD nameRva, nameOff;
        int   allZero = 1, k;

        if (e > pe->size || pe->size - e < 20)
            break;
        for (k = 0; k < 20; ++k)
            if (pe->data[e + k]) { allZero = 0; break; }
        if (allZero)                        /* 全ゼロの記述子が終端 */
            break;

        nameRva = *(const DWORD UNALIGNED *)(pe->data + e + 12);
        if (!RvaToOff(pe, nameRva, &nameOff))
            continue;
        if (!ReadName(pe, nameOff, out[n].name, (DWORD)sizeof(out[n].name)))
            continue;
        out[n].nameOff = nameOff;
        ++n;
    }
    return n;
}

/* 文字列領域の 13 バイトだけを書き換える。ファイル長は変わらない。 */
static BOOL WriteNameField(const WCHAR *path, DWORD off, const char *name)
{
    HANDLE h;
    BYTE   field[NAME_FIELD];
    DWORD  wrote;
    size_t len = 0;

    if (FAILED(StringCchLengthA(name, NAME_FIELD, &len)))
        return FALSE;                       /* 13 バイトに収まらない名前 */

    ZeroMemory(field, sizeof(field));
    memcpy(field, name, len);

    h = CreateFileW(path, GENERIC_WRITE, 0, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return FALSE;

    if (SetFilePointer(h, (LONG)off, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
        !WriteFile(h, field, sizeof(field), &wrote, NULL) ||
        wrote != sizeof(field)) {
        CloseHandle(h);
        return FALSE;
    }
    CloseHandle(h);
    return TRUE;
}

/* ------------------------------------------------------------------ */
/*  状態                                                               */
/* ------------------------------------------------------------------ */

static HWND  g_hwnd, g_drop, g_info, g_btnPatch, g_btnRevert, g_btnBrowse, g_btnClose;
static HFONT g_font;
static int   g_dpi = 96;

static WCHAR g_path[MAX_PATH];
static DWORD g_off;                         /* 書き換える文字列の位置 */
static int   g_state = ST_NONE;

static int S(int v) { return MulDiv(v, g_dpi, 96); }

static void AppendW(WCHAR *buf, size_t cap, const WCHAR *s)
{
    StringCchCatW(buf, cap, s);
}

/* 対象を解析して画面を更新する。 */
static void SetTarget(const WCHAR *path)
{
    BYTE   *data = NULL;
    DWORD   size = 0;
    PEIMG   pe;
    IMPENT  imp[MAX_IMPORTS];
    WCHAR   text[INFO_CHARS];
    int     n, i, hitOrig = -1, hitShim = -1, dupOrig = 0, dupShim = 0;

    g_state = ST_BAD;
    g_off   = 0;
    text[0] = L'\0';

    if (path == NULL || path[0] == L'\0') {
        g_state = ST_NONE;
        AppendW(text, INFO_CHARS,
                L"対象が指定されていません。\r\n\r\n"
                L"" GAME_EXE L" をこのウィンドウにドラッグ&ドロップするか、\r\n"
                L"[参照...] で選んでください。");
        g_path[0] = L'\0';
        goto done;
    }

    StringCchCopyW(g_path, MAX_PATH, path);

    if (!ReadWholeFile(g_path, &data, &size)) {
        AppendW(text, INFO_CHARS, L"ファイルを読めませんでした:\r\n  ");
        AppendW(text, INFO_CHARS, g_path);
        goto done;
    }
    if (!PeParse(data, size, &pe)) {
        AppendW(text, INFO_CHARS,
                L"32bit の Windows 実行ファイル (PE32) ではありません:\r\n  ");
        AppendW(text, INFO_CHARS, g_path);
        goto done;
    }

    n = PeImports(&pe, imp, MAX_IMPORTS);
    if (n == 0) {
        AppendW(text, INFO_CHARS, L"インポートテーブルを読み取れませんでした。");
        goto done;
    }

    AppendW(text, INFO_CHARS, L"対象: ");
    AppendW(text, INFO_CHARS, g_path);
    AppendW(text, INFO_CHARS, L"\r\n\r\nインポート DLL:\r\n");

    for (i = 0; i < n; ++i) {
        WCHAR w[80];
        int   len = MultiByteToWideChar(CP_ACP, 0, imp[i].name, -1, w,
                                        (int)(sizeof(w) / sizeof(w[0])));
        if (len <= 0)
            continue;

        AppendW(text, INFO_CHARS, L"    ");
        AppendW(text, INFO_CHARS, w);

        if (lstrcmpiA(imp[i].name, kOrig) == 0) {
            if (hitOrig < 0) hitOrig = i; else dupOrig = 1;
            AppendW(text, INFO_CHARS, L"   <- 未パッチ");
        } else if (lstrcmpiA(imp[i].name, kShim) == 0) {
            if (hitShim < 0) hitShim = i; else dupShim = 1;
            AppendW(text, INFO_CHARS, L"   <- パッチ済み");
        }
        AppendW(text, INFO_CHARS, L"\r\n");
    }

    AppendW(text, INFO_CHARS, L"\r\n");

    if (dupOrig || dupShim) {
        AppendW(text, INFO_CHARS,
                L"状態: 対象の DLL 名が複数あります。安全のため何もしません。");
    } else if (hitShim >= 0) {
        g_state = ST_PATCHED;
        g_off   = imp[hitShim].nameOff;
        AppendW(text, INFO_CHARS,
                L"状態: パッチ済みです。ムービー修正は有効です。\r\n"
                L"      元に戻すには [元に戻す] を押してください。");
    } else if (hitOrig >= 0) {
        g_state = ST_UNPATCHED;
        g_off   = imp[hitOrig].nameOff;
        AppendW(text, INFO_CHARS,
                L"状態: 未パッチです。\r\n"
                L"      [パッチ適用] を押すと tasoavi.dll を経由するようになります。");
    } else {
        AppendW(text, INFO_CHARS,
                L"状態: この exe は AVIFIL32.dll を使っていません。対象外です。");
    }

done:
    if (data)
        HeapFree(GetProcessHeap(), 0, data);

    SetWindowTextW(g_info, text);
    EnableWindow(g_btnPatch,  g_state == ST_UNPATCHED);
    EnableWindow(g_btnRevert, g_state == ST_PATCHED);
}

/* パッチ適用 / 復元 */
static void DoPatch(BOOL revert)
{
    WCHAR bak[MAX_PATH];
    WCHAR msg[512];
    const char *to = revert ? kOrig : kShim;

    if ((revert && g_state != ST_PATCHED) || (!revert && g_state != ST_UNPATCHED))
        return;

    /* バックアップ。既にあれば作り直さない (出荷時の姿を残すため)。 */
    if (SUCCEEDED(StringCchPrintfW(bak, MAX_PATH, L"%s.bak", g_path))) {
        if (!CopyFileW(g_path, bak, TRUE) &&
            GetLastError() != ERROR_FILE_EXISTS) {
            MessageBoxW(g_hwnd,
                        L"バックアップ (.bak) を作成できませんでした。\r\n"
                        L"書き込み権限のある場所か確認してください。",
                        APP_TITLE, MB_ICONERROR | MB_OK);
            return;
        }
    }

    if (!WriteNameField(g_path, g_off, to)) {
        MessageBoxW(g_hwnd,
                    L"書き換えに失敗しました。\r\n"
                    L"ゲームが起動中でないか、読み取り専用でないか確認してください。",
                    APP_TITLE, MB_ICONERROR | MB_OK);
        SetTarget(g_path);
        return;
    }

    StringCchPrintfW(msg, 512,
                     revert ? L"元に戻しました。\r\n\r\n%S -> %S"
                            : L"パッチを適用しました。\r\n\r\n%S -> %S\r\n\r\n"
                              L"tasoavi.dll をゲーム本体と同じフォルダに置いてください。",
                     revert ? kShim : kOrig, to);
    MessageBoxW(g_hwnd, msg, APP_TITLE, MB_ICONINFORMATION | MB_OK);

    SetTarget(g_path);
}

/* ------------------------------------------------------------------ */
/*  対象の見つけ方                                                     */
/* ------------------------------------------------------------------ */

/* フォルダを渡されたらその中のゲーム exe を探す。 */
static BOOL ResolveDropped(const WCHAR *in, WCHAR *out, size_t cap)
{
    DWORD attr = GetFileAttributesW(in);

    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        WCHAR cand[MAX_PATH];
        if (FAILED(StringCchPrintfW(cand, MAX_PATH, L"%s\\%s", in, GAME_EXE)))
            return FALSE;
        if (GetFileAttributesW(cand) == INVALID_FILE_ATTRIBUTES)
            return FALSE;
        StringCchCopyW(out, cap, cand);
        return TRUE;
    }
    StringCchCopyW(out, cap, in);
    return TRUE;
}

/* 起動時、自分の隣とその親にゲーム exe があれば拾う。 */
static BOOL AutoDetect(WCHAR *out, size_t cap)
{
    WCHAR dir[MAX_PATH], cand[MAX_PATH];
    int   level;

    if (GetModuleFileNameW(NULL, dir, MAX_PATH) == 0)
        return FALSE;

    for (level = 0; level < 2; ++level) {
        WCHAR *slash = wcsrchr(dir, L'\\');
        if (slash == NULL)
            return FALSE;
        *slash = L'\0';
        if (FAILED(StringCchPrintfW(cand, MAX_PATH, L"%s\\%s", dir, GAME_EXE)))
            return FALSE;
        if (GetFileAttributesW(cand) != INVALID_FILE_ATTRIBUTES) {
            StringCchCopyW(out, cap, cand);
            return TRUE;
        }
    }
    return FALSE;
}

static void Browse(void)
{
    OPENFILENAMEW ofn;
    WCHAR         file[MAX_PATH];

    ZeroMemory(&ofn, sizeof(ofn));
    ZeroMemory(file, sizeof(file));
    if (g_path[0])
        StringCchCopyW(file, MAX_PATH, g_path);

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = g_hwnd;
    ofn.lpstrFilter = L"実行ファイル (*.exe)\0*.exe\0すべてのファイル\0*.*\0";
    ofn.lpstrFile   = file;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = L"パッチを当てる exe を選んでください";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    if (GetOpenFileNameW(&ofn))
        SetTarget(file);
}

/* ------------------------------------------------------------------ */
/*  ウィンドウ                                                         */
/* ------------------------------------------------------------------ */

static HWND MakeChild(HWND parent, const WCHAR *cls, const WCHAR *text,
                      DWORD style, int x, int y, int w, int h, int id)
{
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                             S(x), S(y), S(w), S(h),
                             parent, (HMENU)(INT_PTR)id, NULL, NULL);
    if (c)
        SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

static void CreateChildren(HWND hwnd)
{
    MakeChild(hwnd, L"STATIC",
              L"ムービー再生の修正パッチを "GAME_EXE L" に適用します。",
              SS_LEFT | SS_NOPREFIX, 16, 12, 528, 20, -1);

    g_drop = MakeChild(hwnd, L"STATIC",
                       L"ここに " GAME_EXE L" かゲームのフォルダを\r\nドラッグ && ドロップ",
                       SS_CENTER | SS_CENTERIMAGE | SS_NOPREFIX | WS_BORDER,
                       16, 38, 528, 56, ID_DROP);

    g_info = MakeChild(hwnd, L"EDIT", L"",
                       ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL |
                       WS_VSCROLL | WS_BORDER | WS_TABSTOP,
                       16, 104, 528, 156, ID_INFO);

    g_btnPatch  = MakeChild(hwnd, L"BUTTON", L"パッチ適用",
                            BS_PUSHBUTTON | WS_TABSTOP, 16, 272, 120, 30, ID_PATCH);
    g_btnRevert = MakeChild(hwnd, L"BUTTON", L"元に戻す",
                            BS_PUSHBUTTON | WS_TABSTOP, 144, 272, 120, 30, ID_REVERT);
    g_btnBrowse = MakeChild(hwnd, L"BUTTON", L"参照...",
                            BS_PUSHBUTTON | WS_TABSTOP, 272, 272, 120, 30, ID_BROWSE);
    g_btnClose  = MakeChild(hwnd, L"BUTTON", L"閉じる",
                            BS_PUSHBUTTON | WS_TABSTOP, 424, 272, 120, 30, ID_CLOSE);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_hwnd = hwnd;
        CreateChildren(hwnd);
        DragAcceptFiles(hwnd, TRUE);
        return 0;

    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wp;
        WCHAR raw[MAX_PATH], resolved[MAX_PATH];

        if (DragQueryFileW(hDrop, 0, raw, MAX_PATH) > 0) {
            if (ResolveDropped(raw, resolved, MAX_PATH))
                SetTarget(resolved);
            else
                MessageBoxW(hwnd,
                            L"そのフォルダに " GAME_EXE L" が見つかりません。",
                            APP_TITLE, MB_ICONWARNING | MB_OK);
        }
        DragFinish(hDrop);
        return 0;
    }

    case WM_CTLCOLORSTATIC:
        if ((HWND)lp == g_info || (HWND)lp == g_drop) {
            SetBkColor((HDC)wp, GetSysColor(COLOR_WINDOW));
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_PATCH:  DoPatch(FALSE); return 0;
        case ID_REVERT: DoPatch(TRUE);  return 0;
        case ID_BROWSE: Browse();       return 0;
        case ID_CLOSE:  DestroyWindow(hwnd); return 0;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, LPWSTR lpCmd, int nShow)
{
    WNDCLASSEXW       wc;
    INITCOMMONCONTROLSEX icc;
    NONCLIENTMETRICSW ncm;
    RECT              rc;
    HWND              hwnd;
    MSG               msg;
    WCHAR             initial[MAX_PATH];
    int               argc = 0;
    LPWSTR           *argv;
    HDC               hdc;

    (void)hPrev; (void)lpCmd;

    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    hdc = GetDC(NULL);
    if (hdc) {
        g_dpi = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(NULL, hdc);
    }

    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
        g_font = CreateFontIndirectW(&ncm.lfMessageFont);
    if (g_font == NULL)
        g_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"TasoPatchWnd";
    wc.hIcon         = LoadIconW(NULL, IDI_APPLICATION);
    wc.hIconSm       = wc.hIcon;
    if (!RegisterClassExW(&wc))
        return 1;

    rc.left = 0; rc.top = 0; rc.right = S(560); rc.bottom = S(316);
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);

    hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, wc.lpszClassName, APP_TITLE,
                           WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                           CW_USEDEFAULT, CW_USEDEFAULT,
                           rc.right - rc.left, rc.bottom - rc.top,
                           NULL, NULL, hInst, NULL);
    if (hwnd == NULL)
        return 1;

    /* 対象の初期値: コマンドライン > 自動検出 */
    initial[0] = L'\0';
    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        if (argc >= 2)
            ResolveDropped(argv[1], initial, MAX_PATH);
        LocalFree(argv);
    }
    if (initial[0] == L'\0')
        AutoDetect(initial, MAX_PATH);

    SetTarget(initial[0] ? initial : NULL);

    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return 0;
}
