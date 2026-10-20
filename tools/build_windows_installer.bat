@echo off
REM OpenVegas Effects Windows installer build script
REM Requires: CMake, Qt6 (Widgets+Svg), NSIS
REM
REM Optional environment:
REM   CMAKE_PREFIX_PATH           root of the Qt kit, e.g. C:\Qt\6.9.3\mingw_64
REM   OPENVEGASEFFECTS_CMAKE_GENERATOR   CMake generator, e.g. "Ninja" or "MinGW Makefiles"
REM                               (unset = let CMake pick its platform default)

setlocal enabledelayedexpansion

REM UTF-8 for the console, restored on the way out so we don't leave the
REM caller's shell in a different code page.
for /f "tokens=2 delims=:" %%c in ('chcp') do set "ORIG_CP=%%c"
set "ORIG_CP=%ORIG_CP: =%"
chcp 65001 >nul

set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%.."
REM Deliberately NOT the dev tree from CMakePresets (build\Windows_MinGW-x64):
REM that one is configured Debug, and reusing it would flip its cache to Release
REM and force a full rebuild every time you switch between the two.
set "BUILD_DIR=%PROJECT_ROOT%\build\windows-installer"
set "INSTALL_DIR=%BUILD_DIR%\install"
set "NSIS_SCRIPT=%SCRIPT_DIR%nsis_installer.nsi"

echo ========================================
echo OpenVegas Effects - Windows Installer Builder
echo ========================================
echo.

REM NOTE on the "!VAR!" forms below: inside a parenthesised block cmd expands
REM %ERRORLEVEL% once, when it parses the whole block — i.e. before the command
REM in the block has run. Every ERRORLEVEL test that lives inside ( ) must use
REM delayed expansion or it silently reads a stale value.

set "MAKENSIS="
if exist "C:\Program Files (x86)\NSIS\makensis.exe" (
    set "MAKENSIS=C:\Program Files (x86)\NSIS\makensis.exe"
) else if exist "C:\Program Files\NSIS\makensis.exe" (
    set "MAKENSIS=C:\Program Files\NSIS\makensis.exe"
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

set "CMAKE_CMD="
if exist "C:\Program Files\CMake\bin\cmake.exe" (
    set "CMAKE_CMD=C:\Program Files\CMake\bin\cmake.exe"
) else if exist "C:\Program Files (x86)\CMake\bin\cmake.exe" (
    set "CMAKE_CMD=C:\Program Files (x86)\CMake\bin\cmake.exe"
)
if not defined CMAKE_CMD (
    for %%e in (Community Professional Enterprise BuildTools) do (
        set "VS_CMAKE=C:\Program Files\Microsoft Visual Studio\2022\%%e\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        if not defined CMAKE_CMD if exist "!VS_CMAKE!" set "CMAKE_CMD=!VS_CMAKE!"
    )
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

set "EXTRA_CMAKE="
if defined CMAKE_PREFIX_PATH set "EXTRA_CMAKE=-DCMAKE_PREFIX_PATH=%CMAKE_PREFIX_PATH%"
set "GENERATOR_ARG="
if defined OPENVEGASEFFECTS_CMAKE_GENERATOR set "GENERATOR_ARG=-G "%OPENVEGASEFFECTS_CMAKE_GENERATOR%""

"%CMAKE_CMD%" -S "%PROJECT_ROOT%" -B "%BUILD_DIR%" %GENERATOR_ARG% ^
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="%INSTALL_DIR%" %EXTRA_CMAKE%
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] CMake configure failed^^!
    goto :fail
)

echo [2/5] Building...
"%CMAKE_CMD%" --build "%BUILD_DIR%" --config Release
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Build failed^^!
    goto :fail
)

echo [3/5] Installing to %INSTALL_DIR%...
"%CMAKE_CMD%" --install "%BUILD_DIR%" --config Release
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Install failed^^!
    goto :fail
)

echo [4/5] Copying Qt dependencies ^(windeployqt^)...
if not exist "%INSTALL_DIR%\bin\OpenVegasEffects.exe" (
    echo [ERROR] OpenVegasEffects.exe not found in %INSTALL_DIR%\bin\
    goto :fail
)

REM Ask the build tree which Qt it actually used (Qt6_DIR looks like
REM C:/Qt/6.9.3/mingw_64/lib/cmake/Qt6) instead of guessing. Guessing gets this
REM wrong on machines with several kits side by side — a plain scan of C:\Qt
REM hits llvm-mingw_64 before mingw_64 — and deploying the DLLs of a kit you did
REM not build against produces an installer that dies on startup.
set "WINDEPLOYQT="
if exist "%BUILD_DIR%\CMakeCache.txt" (
    for /f "tokens=2 delims==" %%i in ('findstr /b /c:"Qt6_DIR:" "%BUILD_DIR%\CMakeCache.txt" 2^>nul') do (
        if not defined QT6_DIR set "QT6_DIR=%%i"
    )
)
if defined QT6_DIR (
    for %%a in ("!QT6_DIR!\..\..\..") do set "QT_ROOT=%%~fa"
    if exist "!QT_ROOT!\bin\windeployqt.exe" set "WINDEPLOYQT=!QT_ROOT!\bin\windeployqt.exe"
)
if not defined WINDEPLOYQT if defined CMAKE_PREFIX_PATH (
    if exist "%CMAKE_PREFIX_PATH%\bin\windeployqt.exe" (
        set "WINDEPLOYQT=%CMAKE_PREFIX_PATH%\bin\windeployqt.exe"
    )
)
if not defined WINDEPLOYQT (
    for /f "delims=" %%i in ('where windeployqt 2^>nul') do (
        if not defined WINDEPLOYQT set "WINDEPLOYQT=%%i"
    )
)
if defined WINDEPLOYQT (
    echo       using !WINDEPLOYQT!
    "!WINDEPLOYQT!" --release "%INSTALL_DIR%\bin\OpenVegasEffects.exe"
    if !ERRORLEVEL! NEQ 0 echo [WARN] windeployqt finished with error
) else (
    echo [WARN] windeployqt not found - installer may lack Qt DLLs
)

echo [5/5] Creating NSIS installer...
pushd "%INSTALL_DIR%"
set "INSTALL_ABS=%CD%"
popd
pushd "%SCRIPT_DIR%"
"%MAKENSIS%" /DBUILD_DIR="%INSTALL_ABS%" "%NSIS_SCRIPT%"
set "NSIS_RC=%ERRORLEVEL%"
popd
if %NSIS_RC% NEQ 0 (
    echo [ERROR] NSIS installer creation failed^^!
    goto :fail
)

echo.
echo ========================================
echo Done^^! Installer: %SCRIPT_DIR%OpenVegasEffects_Setup.exe
echo ========================================
if defined ORIG_CP chcp %ORIG_CP% >nul
endlocal
exit /b 0

:fail
if defined ORIG_CP chcp %ORIG_CP% >nul
endlocal
exit /b 1
