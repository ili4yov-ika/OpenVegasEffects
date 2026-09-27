@echo off
rem VS Code/Zed MSVC entry point, usable from any working directory.
rem Usage: msvc_build.cmd [configure|build|buildall|probe|clean] [preset]
rem clean runs the selected preset's clean target; it preserves CMake metadata.
setlocal EnableExtensions DisableDelayedExpansion
set "OV_ACTION=%~1"
if not defined OV_ACTION set "OV_ACTION=buildall"
set "OV_PRESET=%~2"
if not defined OV_PRESET set "OV_PRESET=windows-msvc-debug"
if not "%~3"=="" goto usage
if /i "%OV_ACTION%"=="configure" goto valid_action
if /i "%OV_ACTION%"=="build" goto valid_action
if /i "%OV_ACTION%"=="buildall" goto valid_action
if /i "%OV_ACTION%"=="probe" goto valid_action
if /i "%OV_ACTION%"=="clean" goto valid_action
goto usage

:valid_action
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
goto build

:configure
cmake --preset "%OV_PRESET%"
goto finished
:buildall
cmake --preset "%OV_PRESET%"
if errorlevel 1 goto finished
:build
cmake --build --preset "%OV_PRESET%" --parallel
goto finished
:probe
cmake --build --preset "%OV_PRESET%" --target hfpl_runtime_probe --parallel
goto finished
:clean
cmake --build --preset "%OV_PRESET%" --target clean
goto finished

:finished
set "OV_EXIT_CODE=%ERRORLEVEL%"
popd
exit /b %OV_EXIT_CODE%
:failed
popd
exit /b 1
:usage
echo [msvc_build] Usage: msvc_build.cmd [configure^|build^|buildall^|probe^|clean] [preset] 1>&2
exit /b 2
