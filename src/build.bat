@echo off
chcp 65001 >nul
rem  tasoavi.dll  --  32bit ビルド
rem  Visual Studio の C++ デスクトップ開発ワークロードが必要です。
setlocal

set "VSDIR=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
set "VSPATH="
if not exist "%VSDIR%\vswhere.exe" (
    echo [ERROR] vswhere.exe が見つかりません。Visual Studio をインストールしてください。
    exit /b 1
)

rem  vswhere をパス付きで for /f のバッククォート内に置くとクォートが壊れるので、
rem  いったんファイルに出してから読む。行継続 (^) も使わない。
"%VSDIR%\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%TEMP%\tasoavi_vs.txt"
set /p VSPATH=<"%TEMP%\tasoavi_vs.txt"
del "%TEMP%\tasoavi_vs.txt" 2>nul

if not defined VSPATH (
    echo [ERROR] C++ ビルドツールが見つかりません。
    exit /b 1
)

call "%VSPATH%\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 (
    echo [ERROR] vcvars32.bat の実行に失敗しました。
    exit /b 1
)

cd /d "%~dp0"
rc /nologo /fo tasoavi.res tasoavi.rc
if errorlevel 1 exit /b 1

cl /nologo /W4 /O2 /MT /utf-8 /LD tasoavi.c tasoavi.res ^
   /Fe:..\tasoavi.dll ^
   /link /DEF:tasoavi.def vfw32.lib
if errorlevel 1 exit /b 1

del /q tasoavi.obj tasoavi.res ..\tasoavi.exp ..\tasoavi.lib 2>nul

rem  --- GUI パッチャ (tasopatch.exe) ---
rc /nologo /fo tasopatch.res tasopatch.rc
if errorlevel 1 exit /b 1

cl /nologo /W4 /O2 /MT /utf-8 /DUNICODE /D_UNICODE tasopatch.c tasopatch.res ^
   /Fe:..\tasopatch.exe ^
   /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comdlg32.lib comctl32.lib shell32.lib
if errorlevel 1 exit /b 1

del /q tasopatch.obj tasopatch.res 2>nul

echo.
echo === ビルドしました ===
dir /b ..\tasoavi.dll ..\tasopatch.exe
