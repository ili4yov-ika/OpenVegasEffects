@echo off
rem VS Code/Zed MSVC entry point, usable from any working directory.
rem Usage: msvc_build.cmd [configure|build|buildall|probe|clean|tests] [preset]
rem   preset  windows-msvc-debug (default) or windows-msvc-release, building in
rem           build\Windows_MSVC-x64-Debug or build\Windows_MSVC-x64-Release
rem           (every build tree is build\<OS>_<compiler>-<arch>-<configuration>).
rem   build   configures first when the preset's tree has not been configured.
rem   clean   runs the preset's clean target; it preserves CMake metadata.
rem   tests   builds the regression tests (tests\) in
rem           build\Windows_MSVC-x64-Release-Tests and runs them with CTest.
setlocal EnableExtensions DisableDelayedExpansion
set "OV_ACTION=%~1"
if not defined OV_ACTION set "OV_ACTION=buildall"
set "OV_PRESET=%~2"
if not defined OV_PRESET set "OV_PRESET=windows-msvc-debug"
if not "%~3"=="" goto usage
set "OV_BINARY_DIR="
if /i "%OV_PRESET%"=="windows-msvc-debug" set "OV_BINARY_DIR=build\Windows_MSVC-x64-Debug"
if /i "%OV_PRESET%"=="windows-msvc-release" set "OV_BINARY_DIR=build\Windows_MSVC-x64-Release"
if not defined OV_BINARY_DIR (
    echo [msvc_build] "%OV_PRESET%" is not an MSVC preset: windows-msvc-debug or windows-msvc-release. 1>&2
    goto usage
)
set "OV_TESTS_DIR=build\Windows_MSVC-x64-Release-Tests"
if /i "%OV_ACTION%"=="configure" goto valid_action
if /i "%OV_ACTION%"=="build" goto valid_action
if /i "%OV_ACTION%"=="buildall" goto valid_action
if /i "%OV_ACTION%"=="probe" goto valid_action
if /i "%OV_ACTION%"=="clean" goto valid_action
if /i "%OV_ACTION%"=="tests" goto valid_action
goto usage

:valid_action
rem Localized cl.exe prints its /showIncludes prefix in the console code page.
rem CMake recorded it as UTF-8, so a caller in another code page would make
rem Ninja drop every header dependency and leave stale objects behind.
chcp 65001 >nul
pushd "%~dp0.."
if errorlevel 1 exit /b 1
where cmake.exe >nul 2>&1
if errorlevel 1 (
    echo [msvc_build] cmake.exe was not found in PATH. 1>&2
    goto failed
)
set "OV_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%OV_VSWHERE%" (
    echo [msvc_build] vswhere.exe was not found: "%OV_VSWHERE%" 1>&2
    goto failed
)
rem A unique file avoids collisions between concurrent editor build tasks.
set "OV_VS_TEMP=%TEMP%\openvegas-vswhere-%RANDOM%-%RANDOM%.txt"
"%OV_VSWHERE%" -latest -version "[17.0,18.0)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%OV_VS_TEMP%"
set "OV_VS_INSTALL="
if exist "%OV_VS_TEMP%" set /p OV_VS_INSTALL=<"%OV_VS_TEMP%"
if exist "%OV_VS_TEMP%" del /q "%OV_VS_TEMP%"
if not defined OV_VS_INSTALL (
    echo [msvc_build] Visual Studio 2022 with the C++ x64 toolset was not found. 1>&2
    goto failed
)
rem Do not reuse an environment inherited from another Visual Studio version.
set "VSCMD_VER="
set "VSINSTALLDIR="
set "VCINSTALLDIR="
set "VCToolsInstallDir="
set "VCToolsVersion="
set "INCLUDE="
set "LIB="
set "LIBPATH="
call "%OV_VS_INSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo [msvc_build] vcvars64.bat failed: "%OV_VS_INSTALL%\VC\Auxiliary\Build\vcvars64.bat" 1>&2
    goto failed
)
if /i "%OV_ACTION%"=="configure" goto configure
if /i "%OV_ACTION%"=="buildall" goto buildall
if /i "%OV_ACTION%"=="probe" goto probe
if /i "%OV_ACTION%"=="clean" goto clean
if /i "%OV_ACTION%"=="tests" goto tests
goto build

:configure
cmake --preset "%OV_PRESET%"
goto finished
:buildall
cmake --preset "%OV_PRESET%"
if errorlevel 1 goto finished
:build
rem A tree not configured yet (a fresh checkout, a deleted build folder).
if not exist "%OV_BINARY_DIR%\build.ninja" cmake --preset "%OV_PRESET%"
if errorlevel 1 goto finished
cmake --build --preset "%OV_PRESET%" --parallel
if not errorlevel 1 echo [msvc_build] Built in %OV_BINARY_DIR%
goto finished
:probe
cmake --build --preset "%OV_PRESET%" --target hfpl_runtime_probe --parallel
goto finished
:clean
cmake --build --preset "%OV_PRESET%" --target clean
goto finished
:tests
rem The regression tests are a project of their own (tests\CMakeLists.txt),
rem built with the Visual Studio generator as CI builds them. They run
rem offscreen and without sound; Qt's DLLs come from the kit.
set "OV_QT=%CMAKE_PREFIX_PATH%"
if not defined OV_QT set "OV_QT=C:\Qt\6.9.3\msvc2022_64"
set "PATH=%OV_QT%\bin;%PATH%"
set "OPENVEGAS_VLC_AUDIO_OUTPUT=dummy"
cmake -S tests -B "%OV_TESTS_DIR%" -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="%OV_QT%"
if errorlevel 1 goto finished
cmake --build "%OV_TESTS_DIR%" --config Release --parallel
if errorlevel 1 goto finished
ctest --test-dir "%OV_TESTS_DIR%" -C Release --output-on-failure --timeout 900
goto finished

:finished
set "OV_EXIT_CODE=%ERRORLEVEL%"
popd
exit /b %OV_EXIT_CODE%
:failed
popd
exit /b 1
:usage
echo [msvc_build] Usage: msvc_build.cmd [configure^|build^|buildall^|probe^|clean^|tests] [windows-msvc-debug^|windows-msvc-release] 1>&2
exit /b 2
