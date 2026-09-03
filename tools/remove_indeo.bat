@echo off
rem ===================================================================
rem  remove_indeo.bat  --  Unregister and shelve the Indeo codecs.
rem
rem  Run this ONLY after the game's movies have been converted away from
rem  Indeo and confirmed working (see README).
rem
rem  Safe to run either BEFORE or AFTER uninstalling the Indeo entries in
rem  "Installed apps": the COM/DirectShow registrations are removed by
rem  explicit key deletion as well as by regsvr32 /u, so orphaned entries
rem  left behind by an uninstaller are cleaned up too.  Safe to run twice.
rem
rem  Nothing is deleted permanently.  Registry keys are exported first and
rem  the DLLs are MOVED into "indeo_backup" next to this script.
rem
rem  MUST be run as Administrator.
rem ===================================================================
setlocal EnableExtensions EnableDelayedExpansion

net session >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Administrator rights are required.
    echo         Right-click this file and choose "Run as administrator".
    pause
    exit /b 1
)

set "BACKUP=%~dp0indeo_backup"
set "D32=HKLM\SOFTWARE\WOW6432Node\Microsoft\Windows NT\CurrentVersion\Drivers32"
set "DESC=HKLM\SOFTWARE\WOW6432Node\Microsoft\Windows NT\CurrentVersion\drivers.desc"
set "CLS=HKLM\SOFTWARE\Classes\WOW6432Node\CLSID"
set "DSCAT=%CLS%\{083863F1-70DE-11D0-BD40-00A0C911CE86}\Instance"

rem  Every CLSID whose InprocServer32 points at an Indeo binary.
set GUIDS={1F73E9B1-8C3A-11D0-A3BE-00A0C9244436} {2DE89781-DBF6-11D0-A30E-444553540000}
set GUIDS=%GUIDS% {30355649-0000-0010-8000-00AA00389B71} {31345649-0000-0010-8000-00AA00389B71}
set GUIDS=%GUIDS% {665A4443-D905-11D0-A30E-444553540000} {665A4444-D905-11D0-A30E-444553540000}
set GUIDS=%GUIDS% {665A4445-D905-11D0-A30E-444553540000} {665A4448-D905-11D0-A30E-444553540000}
set GUIDS=%GUIDS% {665A444A-D905-11D0-A30E-444553540000} {84725EA1-2FBC-11D1-BC86-00A0C969FC67}
set GUIDS=%GUIDS% {87CA6F02-49E4-11CF-A3FE-00AA003735BE} {87CA6F04-49E4-11CF-A3FE-00AA003735BE}
set GUIDS=%GUIDS% {A2551F60-705F-11CF-A424-00AA003735BE} {B4CA2970-DD2B-11D0-9DFA-00AA00AF3494}
set GUIDS=%GUIDS% {B4CA2971-DD2B-11D0-9DFA-00AA00AF3494} {BD323430-CE94-11CE-82DD-0800095A5B55}
set GUIDS=%GUIDS% {BD323431-CE94-11CE-82DD-0800095A5B55} {BD323432-CE94-11CE-82DD-0800095A5B55}
set GUIDS=%GUIDS% {BD323433-CE94-11CE-82DD-0800095A5B55} {C1C0FE00-F3C2-11D0-91D4-444553540000}
set GUIDS=%GUIDS% {C83B5610-E0DF-11D0-9E00-00AA00AF3494} {E369A160-F3C2-11D0-91D4-444553540000}

rem  Every Indeo binary that may sit in a system folder.  The ".2000" ones
rem  exist because one Indeo installer renamed the other one's DLL out of
rem  the way instead of deleting it.
set CODECS=ir50_32.dll ir50_qc.dll ir50_qcx.dll ir41_32.dll ir41_32.ax
set CODECS=%CODECS% ir41_qc.dll ir41_qcx.dll ir32_32.dll iac25_32.ax iyvu9_32.dll
set CODECS=%CODECS% ir50_32.dll.2000 ir41_32.ax.2000 ir32_32.dll.2000 iac25_32.ax.2000

if not exist "%BACKUP%"       mkdir "%BACKUP%"
if not exist "%BACKUP%\clsid" mkdir "%BACKUP%\clsid"

echo.
echo === 1/6  Backing up the registry ===
reg export "%D32%"  "%BACKUP%\Drivers32.reg"    /y >nul 2>&1 && echo   Drivers32.reg
reg export "%DESC%" "%BACKUP%\drivers.desc.reg" /y >nul 2>&1 && echo   drivers.desc.reg
set /a NB=0
for %%G in (%GUIDS%) do (
    reg query "%CLS%\%%G" >nul 2>&1 && (
        reg export "%CLS%\%%G" "%BACKUP%\clsid\%%G.reg" /y >nul 2>&1
        set /a NB+=1
    )
)
echo   %NB% CLSID keys exported to indeo_backup\clsid

echo.
echo === 2/6  Unregistering the DirectShow / ActiveMovie filters ===
for %%F in (ir50_32.dll ir41_32.ax iac25_32.ax ir32_32.dll iyvu9_32.dll) do (
    if exist "%SystemRoot%\SysWOW64\%%F" (
        regsvr32 /u /s "%SystemRoot%\SysWOW64\%%F" >nul 2>&1 && echo   regsvr32 /u SysWOW64\%%F
    )
)

echo.
echo === 3/6  Deleting any remaining COM registrations ===
set /a ND=0
for %%G in (%GUIDS%) do (
    reg delete "%DSCAT%\%%G" /f >nul 2>&1 && set /a ND+=1
    reg delete "HKLM\SOFTWARE\Classes\WOW6432Node\Filter\%%G" /f >nul 2>&1
    reg delete "%CLS%\%%G" /f >nul 2>&1 && (
        set /a ND+=1
        echo   removed %%G
    )
)
if %ND%==0 echo   nothing left to remove

echo.
echo === 4/6  Removing the Video for Windows registrations ===
for %%V in (vidc.iv50 vidc.ir50 vidc.iv41 vidc.ir41 vidc.iv31 vidc.ir31 vidc.iv32 vidc.ir32 vidc.iyvu9 msacm.iac2) do (
    reg delete "%D32%" /v %%V /f >nul 2>&1 && echo   removed %%V
)
for %%V in ("C:\Windows\system32\ir32_32.dll" "C:\Windows\system32\ir41_32.ax" "C:\Windows\system32\ir50_32.dll" "C:\Windows\system32\Iac25_32.ax" "C:\Windows\system32\iyvu9_32.dll" "ir50_32.dll" "ir41_32.ax" "ir32_32.dll" "iyvu9_32.dll") do (
    reg delete "%DESC%" /v %%V /f >nul 2>&1 && echo   removed drivers.desc entry %%V
)

echo.
echo === 5/6  Restoring vidc.yvu9 to the Windows default ===
rem  Windows points vidc.yvu9 at its own signed tsbyuv.dll, which is still
rem  what the 64-bit hive says.  The Indeo installer hijacked the 32-bit
rem  entry (pointing it at iyvu9_32.dll) and its uninstaller then deleted
rem  the value outright instead of putting the original back, leaving
rem  32-bit apps with no YVU9 handler at all.  Fix both cases.
set "YVU9="
for /f "tokens=2*" %%A in ('reg query "%D32%" /v vidc.yvu9 2^>nul ^| findstr /i "vidc.yvu9"') do set "YVU9=%%B"

set "FIXYVU9="
if not defined YVU9 set "FIXYVU9=1"
if /i "%YVU9%"=="iyvu9_32.dll" set "FIXYVU9=1"

if defined FIXYVU9 (
    if exist "%SystemRoot%\SysWOW64\tsbyuv.dll" (
        reg add "%D32%" /v vidc.yvu9 /t REG_SZ /d tsbyuv.dll /f >nul 2>&1 && echo   vidc.yvu9 -^> tsbyuv.dll ^(Windows default^)
    ) else (
        echo   [WARN] SysWOW64\tsbyuv.dll not found; leaving vidc.yvu9 alone
    )
) else (
    echo   vidc.yvu9 = %YVU9%  ^(not Indeo, left alone^)
)

echo.
echo === 6/6  Moving the codec binaries out of the system folders ===
rem  The two system folders hold DIFFERENT builds of the same file names
rem  (SysWOW64 = the 2000 Ligos build, System32 = the repackaged one), so
rem  each is shelved into its own subfolder.  The System32 copies carry the
rem  SYSTEM/HIDDEN attributes, so clear those before moving.
set /a MOVED=0, FAILED=0
for %%D in ("%SystemRoot%\SysWOW64" "%SystemRoot%\System32") do (
    if not exist "%BACKUP%\%%~nxD" mkdir "%BACKUP%\%%~nxD"
    for %%F in (%CODECS%) do (
        if exist "%%~D\%%F" (
            attrib -s -h -r "%%~D\%%F" >nul 2>&1
            move /y "%%~D\%%F" "%BACKUP%\%%~nxD\" >nul 2>&1
            if exist "%%~D\%%F" (
                echo   [FAIL] %%~nxD\%%F
                set /a FAILED+=1
            ) else (
                echo   moved %%~nxD\%%F
                set /a MOVED+=1
            )
        )
    )
)
echo   moved=%MOVED%  failed=%FAILED%
if not "%FAILED%"=="0" (
    echo.
    echo   Some files could not be moved.  The usual cause is anti-virus
    echo   software blocking writes to C:\Windows\System32.
    echo   They are harmless where they are: 32-bit DLLs in the 64-bit
    echo   system folder cannot be loaded by a 64-bit process, and a
    echo   32-bit process is redirected to SysWOW64, which is now clean.
    echo   Nothing in the registry refers to them any more either.
)

echo.
echo === Verification ===
reg query "%D32%" 2>nul | findstr /i "iv50 ir50 iv41 ir41 iv31 ir31 iv32 ir32 iyvu9 iac2" >nul && (
    echo   [WARN] some Drivers32 entries are still present
) || echo   Drivers32 : clean

reg query "%D32%" /v vidc.yvu9 2>nul | findstr /i tsbyuv >nul && (
    echo   vidc.yvu9 : tsbyuv.dll  OK
) || echo   [WARN] vidc.yvu9 is not tsbyuv.dll

set /a LEFT=0
for %%F in (%CODECS%) do (
    if exist "%SystemRoot%\SysWOW64\%%F" set /a LEFT+=1
    if exist "%SystemRoot%\System32\%%F" set /a LEFT+=1
)
if "%LEFT%"=="0" (
    echo   codec files : none left in SysWOW64 / System32
) else (
    echo   [WARN] %LEFT% Indeo file^(s^) still in the system folders
)

echo.
echo === Done ===
echo   Backups: %BACKUP%
echo   Now uninstall the two "Indeo" entries from Installed apps if you
echo   have not already, then start the game and check the movies.
echo.
pause
