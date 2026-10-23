@echo off
REM OpenVegas Effects Windows installer build script
REM Requires: Visual Studio 2022 (or newer) with the C++ x64 toolset, CMake,
REM Qt 6 for MSVC (msvc2022_64), NSIS and a libVLC runtime (VLC installed in
REM Program Files, or OPENVEGAS_VLC_RUNTIME_DIR).
REM
REM Optional environment:
REM   CMAKE_PREFIX_PATH           root of the Qt MSVC kit, e.g. C:\Qt\6.9.3\msvc2022_64
REM                               (unset = C:\Qt\6.9.3\msvc2022_64, else the newest
REM                               C:\Qt\*\msvc2022_64)
REM   OPENVEGAS_VLC_RUNTIME_DIR   prepared libVLC runtime (default: installed VLC)
REM   OPENVEGASEFFECTS_CMAKE_GENERATOR   CMake generator (default: Ninja when found)

setlocal EnableExtensions EnableDelayedExpansion

REM UTF-8 for the console, restored on the way out so we don't leave the
REM caller's shell in a different code page. Some locales end the chcp line
REM with a period ("Aktive Codepage: 850."), so keep the digits only.
set "ORIG_CP="
for /f "tokens=2 delims=:" %%c in ('chcp') do set "ORIG_CP=%%c"
set "ORIG_CP=%ORIG_CP: =%"
set "ORIG_CP=%ORIG_CP:.=%"
chcp 65001 >nul

set "SCRIPT_DIR=%~dp0"
for %%a in ("%SCRIPT_DIR%..") do set "PROJECT_ROOT=%%~fa"
REM Deliberately NOT a dev tree from CMakePresets (build\Windows_MSVC-x64 is
REM Debug, build\Windows_MSVC-x64-Release RelWithDebInfo): reusing one would
REM flip its cache and force a full rebuild every time you switch.
set "BUILD_DIR=%PROJECT_ROOT%\build\windows-installer"
set "INSTALL_DIR=%BUILD_DIR%\install"
set "NSIS_SCRIPT=%SCRIPT_DIR%nsis_installer.nsi"
set "OUTPUT_FILE=%SCRIPT_DIR%OpenVegasEffects_Setup.exe"

echo ========================================
echo OpenVegas Effects - Windows Installer Builder
echo ========================================
echo.

REM NOTE on the "!VAR!" forms below: inside a parenthesised block cmd expands
REM %VAR% once, when it parses the whole block - i.e. before the commands in
REM the block have run. Every value set and read inside ( ) must use delayed
REM expansion or it silently reads a stale value.

set "MAKENSIS="
if exist "%ProgramFiles(x86)%\NSIS\makensis.exe" (
    set "MAKENSIS=%ProgramFiles(x86)%\NSIS\makensis.exe"
) else if exist "%ProgramFiles%\NSIS\makensis.exe" (
    set "MAKENSIS=%ProgramFiles%\NSIS\makensis.exe"
)
if not defined MAKENSIS (
    for /f "delims=" %%i in ('where makensis 2^>nul') do (
        if not defined MAKENSIS set "MAKENSIS=%%i"
    )
)
if not defined MAKENSIS (
    echo [ERROR] NSIS ^(makensis^) not found^^!
    echo Install NSIS: https://nsis.sourceforge.io/Download
    echo Or add NSIS to PATH
    goto :fail
)

REM The MSVC environment. Without it CMake picks whatever Visual Studio
REM generator it finds first and any Qt kit on PATH - a MinGW Qt with the MSVC
REM compiler, which cannot link. Visual Studio 2022 is preferred (the Qt
REM msvc2022_64 kit is built with it); a newer one is binary compatible.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ERROR] vswhere.exe not found - install Visual Studio 2022 with "Desktop development with C++".
    goto :fail
)
set "VS_INSTALL="
REM Through a file: for /f strips the outer quotes of a quoted command line,
REM which breaks the quoted version range.
set "VS_TEMP=%TEMP%\openvegas-vswhere-%RANDOM%-%RANDOM%.txt"
"%VSWHERE%" -latest -products * -version "[17.0,18.0)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%VS_TEMP%"
if exist "%VS_TEMP%" set /p VS_INSTALL=<"%VS_TEMP%"
if not defined VS_INSTALL (
    "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%VS_TEMP%"
    if exist "%VS_TEMP%" set /p VS_INSTALL=<"%VS_TEMP%"
)
if exist "%VS_TEMP%" del /q "%VS_TEMP%"
if not defined VS_INSTALL (
    echo [ERROR] Visual Studio with the C++ x64 toolset was not found.
    goto :fail
)
REM Do not mix with an environment inherited from another Visual Studio.
set "VSCMD_VER="
set "VSINSTALLDIR="
set "VCINSTALLDIR="
set "VCToolsInstallDir="
set "VCToolsRedistDir="
set "VCToolsVersion="
set "INCLUDE="
set "LIB="
set "LIBPATH="
call "%VS_INSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo [ERROR] vcvars64.bat failed: "%VS_INSTALL%\VC\Auxiliary\Build\vcvars64.bat"
    goto :fail
)
echo       MSVC: %VS_INSTALL%
set "VS_INSTALL_CMAKE=%VS_INSTALL:\=/%"

set "CMAKE_CMD="
if exist "%ProgramFiles%\CMake\bin\cmake.exe" (
    set "CMAKE_CMD=%ProgramFiles%\CMake\bin\cmake.exe"
) else if exist "%ProgramFiles(x86)%\CMake\bin\cmake.exe" (
    set "CMAKE_CMD=%ProgramFiles(x86)%\CMake\bin\cmake.exe"
)
if not defined CMAKE_CMD (
    for /f "delims=" %%i in ('where cmake 2^>nul') do (
        if not defined CMAKE_CMD set "CMAKE_CMD=%%i"
    )
)
if not defined CMAKE_CMD (
    echo [ERROR] CMake not found^^! Add CMake to PATH or install it.
    goto :fail
)

REM The Qt kit for MSVC.
set "QT_ROOT="
if defined CMAKE_PREFIX_PATH (
    if exist "%CMAKE_PREFIX_PATH%\lib\cmake\Qt6\Qt6Config.cmake" set "QT_ROOT=%CMAKE_PREFIX_PATH%"
)
if not defined QT_ROOT if exist "C:\Qt\6.9.3\msvc2022_64\lib\cmake\Qt6\Qt6Config.cmake" (
    set "QT_ROOT=C:\Qt\6.9.3\msvc2022_64"
)
if not defined QT_ROOT (
    for /d %%v in ("C:\Qt\6.*") do (
        if exist "%%~fv\msvc2022_64\lib\cmake\Qt6\Qt6Config.cmake" set "QT_ROOT=%%~fv\msvc2022_64"
    )
)
if not defined QT_ROOT (
    echo [ERROR] Qt 6 for MSVC ^(msvc2022_64^) not found. Set CMAKE_PREFIX_PATH to the kit root.
    goto :fail
)
set "QT_ROOT_CMAKE=%QT_ROOT:\=/%"
echo       Qt:   %QT_ROOT%

REM Ninja from the environment or Qt's tools; otherwise CMake's default.
set "GENERATOR=%OPENVEGASEFFECTS_CMAKE_GENERATOR%"
set "NINJA_ARG="
if not defined GENERATOR (
    for /f "delims=" %%i in ('where ninja 2^>nul') do (
        if not defined GENERATOR set "GENERATOR=Ninja"
    )
)
if not defined GENERATOR if exist "C:\Qt\Tools\Ninja\ninja.exe" (
    set "GENERATOR=Ninja"
    set "NINJA_ARG=-DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe"
)

REM Generate logo.ico for EXE icon if missing
if not exist "%PROJECT_ROOT%\resources\icons\logo.ico" (
    echo [0/5] Generating logo.ico from logo.svg...
    set "PY_CMD="
    for /f "delims=" %%i in ('where python 2^>nul') do (
        if not defined PY_CMD set "PY_CMD=%%i"
    )
    if not defined PY_CMD (
        for /f "delims=" %%i in ('where py 2^>nul') do (
            if not defined PY_CMD set "PY_CMD=%%i"
        )
    )
    if defined PY_CMD (
        "!PY_CMD!" "%SCRIPT_DIR%svg_to_ico.py"
    ) else (
        echo [WARN] Python not found - skipping icon generation.
    )
    if not exist "%PROJECT_ROOT%\resources\icons\logo.ico" (
        echo [WARN] logo.ico not created - EXE will use default icon. Install Pillow ^(pip install pillow^).
    )
)

echo [1/5] CMake configure...
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

REM A cache from another generator, compiler or Qt kit cannot be
REM reconfigured in place (CMake refuses a generator change and keeps the
REM cached compiler and Qt paths): start that build tree over.
if exist "%BUILD_DIR%\CMakeCache.txt" (
    set "STALE="
    set "CACHED_QT="
    set "CACHED_GENERATOR="
    set "CACHED_CXX="
    for /f "tokens=2 delims==" %%i in ('findstr /b /c:"Qt6_DIR:" "%BUILD_DIR%\CMakeCache.txt" 2^>nul') do set "CACHED_QT=%%i"
    for /f "tokens=2 delims==" %%i in ('findstr /b /c:"CMAKE_GENERATOR:" "%BUILD_DIR%\CMakeCache.txt" 2^>nul') do set "CACHED_GENERATOR=%%i"
    for /f "tokens=2 delims==" %%i in ('findstr /b /c:"CMAKE_CXX_COMPILER:FILEPATH=" "%BUILD_DIR%\CMakeCache.txt" 2^>nul') do set "CACHED_CXX=%%i"
    if /i not "!CACHED_QT!"=="%QT_ROOT_CMAKE%/lib/cmake/Qt6" set "STALE=1"
    if defined GENERATOR if /i not "!CACHED_GENERATOR!"=="%GENERATOR%" set "STALE=1"
    REM The compiler must be this Visual Studio's (its path does not
    REM change when the prefix is removed from it otherwise).
    if defined CACHED_CXX (
        set "CXX_REST=!CACHED_CXX:%VS_INSTALL_CMAKE%/=!"
        if /i "!CXX_REST!"=="!CACHED_CXX!" set "STALE=1"
    )
    if defined STALE (
        echo       build tree was configured with !CACHED_GENERATOR! / !CACHED_QT! - starting it over
        del /q "%BUILD_DIR%\CMakeCache.txt"
        if exist "%BUILD_DIR%\CMakeFiles" rmdir /s /q "%BUILD_DIR%\CMakeFiles"
    )
)

set "EXTRA_CMAKE="
if defined OPENVEGAS_VLC_RUNTIME_DIR set "EXTRA_CMAKE=-DOPENVEGAS_VLC_RUNTIME_DIR=%OPENVEGAS_VLC_RUNTIME_DIR:\=/%"
set "GENERATOR_ARG="
if defined GENERATOR set GENERATOR_ARG=-G "%GENERATOR%"

"%CMAKE_CMD%" -S "%PROJECT_ROOT%" -B "%BUILD_DIR%" %GENERATOR_ARG% %NINJA_ARG% ^
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="%INSTALL_DIR%" ^
    -DCMAKE_PREFIX_PATH="%QT_ROOT_CMAKE%" %EXTRA_CMAKE%
if errorlevel 1 (
    echo [ERROR] CMake configure failed^^!
    goto :fail
)

echo [2/5] Building...
"%CMAKE_CMD%" --build "%BUILD_DIR%" --config Release --target OpenVegasEffects --parallel
if errorlevel 1 (
    echo [ERROR] Build failed^^!
    goto :fail
)

echo [3/5] Installing to %INSTALL_DIR%...
REM A fresh prefix, so files of an earlier build do not reach the installer.
if exist "%INSTALL_DIR%" rmdir /s /q "%INSTALL_DIR%"
"%CMAKE_CMD%" --install "%BUILD_DIR%" --config Release
if errorlevel 1 (
    echo [ERROR] Install failed^^!
    goto :fail
)

echo [4/5] Copying Qt and MSVC runtime dependencies...
if not exist "%INSTALL_DIR%\bin\OpenVegasEffects.exe" (
    echo [ERROR] OpenVegasEffects.exe not found in %INSTALL_DIR%\bin\
    goto :fail
)
if not exist "%QT_ROOT%\bin\windeployqt.exe" (
    echo [ERROR] windeployqt.exe not found in %QT_ROOT%\bin
    goto :fail
)
REM windeployqt would only add vc_redist.x64.exe, which the installer never
REM runs; the runtime DLLs are copied next to the EXE instead.
"%QT_ROOT%\bin\windeployqt.exe" --release --no-compiler-runtime "%INSTALL_DIR%\bin\OpenVegasEffects.exe"
if errorlevel 1 (
    echo [ERROR] windeployqt failed^^!
    goto :fail
)
set "CRT_DIR="
if defined VCToolsRedistDir (
    for /d %%d in ("%VCToolsRedistDir%x64\Microsoft.VC*.CRT") do set "CRT_DIR=%%~fd"
)
if not defined CRT_DIR (
    echo [ERROR] MSVC runtime ^(Microsoft.VC*.CRT^) not found under "%VCToolsRedistDir%x64".
    goto :fail
)
copy /y "%CRT_DIR%\*.dll" "%INSTALL_DIR%\bin\" >nul
if errorlevel 1 (
    echo [ERROR] Copying the MSVC runtime failed^^!
    goto :fail
)

echo [5/5] Creating NSIS installer...
"%MAKENSIS%" /DBUILD_DIR="%INSTALL_DIR%" /DOUTPUT_FILE="%OUTPUT_FILE%" "%NSIS_SCRIPT%"
if errorlevel 1 (
    echo [ERROR] NSIS installer creation failed^^!
    goto :fail
)

echo.
echo ========================================
echo Done^^! Installer: %OUTPUT_FILE%
echo ========================================
if defined ORIG_CP chcp %ORIG_CP% >nul
endlocal
exit /b 0

:fail
if defined ORIG_CP chcp %ORIG_CP% >nul
endlocal
exit /b 1
