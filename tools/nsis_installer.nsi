; -*- coding: utf-8 -*-
; Stage cmake --install, then windeployqt on <prefix>/bin.
; makensis /DBUILD_DIR="D:\path\install" [/DOUTPUT_FILE="D:\path\Setup.exe"] tools\nsis_installer.nsi
; Relative build/output paths are resolved against the repository root.
Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"
; makensis enters the script's directory before preprocessing (do not use /NOCD).
; __FILEDIR__ may be relative to the original caller, so do not append it again.
!cd ".."
!ifndef BUILD_DIR
  !error "BUILD_DIR is required: pass /DBUILD_DIR=<staged-install-prefix> containing bin."
!endif
!if ! /FileExists "${BUILD_DIR}\bin\OpenVegasEffects.exe"
  !error "BUILD_DIR/bin/OpenVegasEffects.exe is missing. Run cmake --install first."
!endif
!if ! /FileExists "${BUILD_DIR}\bin\platforms\qwindows.dll"
  !error "Qt platform plugin is missing. Run windeployqt on BUILD_DIR/bin."
!endif
!if ! /FileExists "${BUILD_DIR}\bin\vlc\libvlc.dll"
  !error "libVLC is missing. Configure OPENVEGAS_VLC_RUNTIME_DIR and stage the runtime."
!endif
!if ! /FileExists "${BUILD_DIR}\bin\vlc\libvlccore.dll"
  !error "libVLC core is missing from BUILD_DIR/bin/vlc."
!endif
!ifndef OUTPUT_FILE
  !define OUTPUT_FILE "${BUILD_DIR}\..\OpenVegasEffects_Setup.exe"
!endif
!define PRODUCT_NAME "OpenVegas Effects"
!ifndef PRODUCT_VERSION
  !define PRODUCT_VERSION "0.1.0"
!endif
!define PRODUCT_PUBLISHER "OpenVegas Effects contributors"
!define PRODUCT_WEB_SITE "https://github.com/ili4yov-ika/OpenVegasEffects"
!define APP_KEY "Software\${PRODUCT_NAME}"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"
!define PROJECT_PROGID "OpenVegasEffects.Project"

; Inventory includes WebEngine, translations and optional native plugins.
; Uninstall removes only packaged files and directories that are empty.
!tempfile OV_PAYLOAD_INCLUDE
!system 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "tools\nsis_payload.ps1" -Source "${BUILD_DIR}\bin" -Output "${OV_PAYLOAD_INCLUDE}" -RequireVlc' = 0
!include /CHARSET=UTF8 "${OV_PAYLOAD_INCLUDE}"
!delfile "${OV_PAYLOAD_INCLUDE}"

Name "${PRODUCT_NAME}"
OutFile "${OUTPUT_FILE}"
InstallDir "$PROGRAMFILES64\${PRODUCT_NAME}"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
!define MUI_ICON "resources\icons\logo.ico"
!define MUI_UNICON "resources\icons\logo.ico"
!define MUI_ABORTWARNING
!define MUI_LANGDLL_REGISTRY_ROOT HKLM
!define MUI_LANGDLL_REGISTRY_KEY "${APP_KEY}"
!define MUI_LANGDLL_REGISTRY_VALUENAME "InstallerLanguage"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "Russian"
!insertmacro MUI_LANGUAGE "English"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "${PRODUCT_NAME} requires 64-bit Windows."
    Abort
  ${EndIf}
  SetRegView 64
  SetShellVarContext all
  ; Preserve a custom /D= directory supplied on the command line.
  ${If} $INSTDIR == "$PROGRAMFILES64\${PRODUCT_NAME}"
    ReadRegStr $0 HKLM "${APP_KEY}" "InstallPath"
    ${If} $0 != ""
      StrCpy $INSTDIR $0
    ${EndIf}
  ${EndIf}
  !insertmacro MUI_LANGDLL_DISPLAY
FunctionEnd

Function ValidateInstallDirectory
  GetFullPathName $0 "$INSTDIR"
  ${GetRoot} "$0" $1
  ${If} $0 == "$1\"
  ${OrIf} $0 == "$PROGRAMFILES64"
  ${OrIf} $0 == "$PROGRAMFILES32"
  ${OrIf} $0 == "$WINDIR"
  ${OrIf} $0 == "$SYSDIR"
    MessageBox MB_OK|MB_ICONSTOP "Choose a dedicated application folder."
    Abort
  ${EndIf}
FunctionEnd
Function .onVerifyInstDir
  Call ValidateInstallDirectory
FunctionEnd

Section "${PRODUCT_NAME}" SEC_APP
  SectionIn RO
  Call ValidateInstallDirectory
  SetOutPath "$INSTDIR"
  SetOverwrite on
  !insertmacro OV_INSTALL_PAYLOAD
  ; Reset the working directory after copying nested Qt/plugin folders.
  SetOutPath "$INSTDIR"
  File "LICENSE"
  ; App .qm catalogues are embedded; deployed Qt/external catalogues are
  ; included automatically when present in the staged bin tree.
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "${APP_KEY}" "InstallPath" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "${PRODUCT_NAME}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
  WriteRegStr HKLM "${UNINSTALL_KEY}" "QuietUninstallString" '$\"$INSTDIR\Uninstall.exe$\" /S'
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayIcon" '$\"$INSTDIR\OpenVegasEffects.exe$\",0'
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher" "${PRODUCT_PUBLISHER}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "URLInfoAbout" "${PRODUCT_WEB_SITE}"
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1
  CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
  CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk" "$INSTDIR\OpenVegasEffects.exe"
  CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall ${PRODUCT_NAME}.lnk" "$INSTDIR\Uninstall.exe"

  ReadRegStr $0 HKCR ".vegfx" ""
  ${If} $0 != "${PROJECT_PROGID}"
    WriteRegStr HKLM "${APP_KEY}" "PreviousVegfxProgId" $0
  ${EndIf}
  WriteRegStr HKCR ".vegfx" "" "${PROJECT_PROGID}"
  WriteRegStr HKCR "${PROJECT_PROGID}" "" "OpenVegas Effects Project"
  WriteRegStr HKCR "${PROJECT_PROGID}\DefaultIcon" "" '$\"$INSTDIR\OpenVegasEffects.exe$\",0'
  WriteRegStr HKCR "${PROJECT_PROGID}\shell\open\command" "" '$\"$INSTDIR\OpenVegasEffects.exe$\" $\"%1$\"'
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
SectionEnd

Function un.onInit
  SetRegView 64
  SetShellVarContext all
  !insertmacro MUI_UNGETLANGUAGE
  ReadRegStr $0 HKLM "${APP_KEY}" "InstallPath"
  GetFullPathName $1 "$INSTDIR"
  GetFullPathName $2 "$0"
  ${If} $0 == ""
  ${OrIf} $1 != $2
    MessageBox MB_OK|MB_ICONSTOP "Run the uninstaller from the registered application folder."
    Abort
  ${EndIf}
FunctionEnd

Section "Uninstall"
  !insertmacro OV_UNINSTALL_PAYLOAD
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk"
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall ${PRODUCT_NAME}.lnk"
  RMDir "$SMPROGRAMS\${PRODUCT_NAME}"
  ReadRegStr $0 HKCR ".vegfx" ""
  ${If} $0 == "${PROJECT_PROGID}"
    ReadRegStr $1 HKLM "${APP_KEY}" "PreviousVegfxProgId"
    ${If} $1 == ""
      DeleteRegValue HKCR ".vegfx" ""
      DeleteRegKey /ifempty HKCR ".vegfx"
    ${Else}
      WriteRegStr HKCR ".vegfx" "" $1
    ${EndIf}
  ${EndIf}
  ReadRegStr $0 HKCR "${PROJECT_PROGID}\shell\open\command" ""
  ${If} $0 == '$\"$INSTDIR\OpenVegasEffects.exe$\" $\"%1$\"'
    DeleteRegKey HKCR "${PROJECT_PROGID}"
  ${EndIf}
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
  DeleteRegKey HKLM "${UNINSTALL_KEY}"
  DeleteRegKey HKLM "${APP_KEY}"
SectionEnd
