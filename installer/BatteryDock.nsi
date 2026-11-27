; BatteryDock installer (NSIS 3). Per-user install, no admin rights needed.
; Build: makensis -DVERSION=1.1.0 installer/BatteryDock.nsi   (expects BatteryDock.exe in the repo root)
Unicode true
!ifndef VERSION
  !define VERSION "1.1.0"
!endif
!define APP "BatteryDock"
!define UNINST "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP}"

Name "${APP}"
OutFile "..\dist\${APP}-Setup-${VERSION}.exe"
InstallDir "$LOCALAPPDATA\Programs\${APP}"
InstallDirRegKey HKCU "Software\${APP}" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
Icon "..\res\app.ico"
UninstallIcon "..\res\app.ico"
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${APP}"
VIAddVersionKey "FileDescription" "${APP} Setup"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "GPL-3.0 - based on BatteryHub by krisgfx9"

!include "MUI2.nsh"
!define MUI_ICON "..\res\app.ico"
!define MUI_UNICON "..\res\app.ico"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP}.exe"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "Turkish"
!insertmacro MUI_LANGUAGE "English"

LangString SecApp ${LANG_TURKISH} "BatteryDock (gerekli)"
LangString SecApp ${LANG_ENGLISH} "BatteryDock (required)"
LangString SecStart ${LANG_TURKISH} "Başlat menüsü kısayolu"
LangString SecStart ${LANG_ENGLISH} "Start menu shortcut"
LangString SecAuto ${LANG_TURKISH} "Windows ile başlat"
LangString SecAuto ${LANG_ENGLISH} "Start with Windows"

Function .onInit
  !insertmacro MUI_LANGDLL_DISPLAY
FunctionEnd

Section "!$(SecApp)" SecMain
  SectionIn RO
  nsExec::Exec 'taskkill /IM ${APP}.exe /F'
  Sleep 500
  SetOutPath "$INSTDIR"
  File "..\${APP}.exe"
  File "..\LICENSE"
  File "THIRD_PARTY_NOTICES.txt"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "Software\${APP}" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "${UNINST}" "DisplayName" "${APP}"
  WriteRegStr HKCU "${UNINST}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINST}" "Publisher" "NOCTAB Labs"
  WriteRegStr HKCU "${UNINST}" "DisplayIcon" "$INSTDIR\${APP}.exe"
  WriteRegStr HKCU "${UNINST}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKCU "${UNINST}" "InstallLocation" "$INSTDIR"
  WriteRegDWORD HKCU "${UNINST}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST}" "NoRepair" 1
SectionEnd

Section "$(SecStart)" SecShortcut
  CreateShortcut "$SMPROGRAMS\${APP}.lnk" "$INSTDIR\${APP}.exe"
SectionEnd

Section "$(SecAuto)" SecAutostart
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "${APP}" '"$INSTDIR\${APP}.exe"'
SectionEnd

Section "Uninstall"
  nsExec::Exec 'taskkill /IM ${APP}.exe /F'
  Sleep 500
  DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "${APP}"
  Delete "$SMPROGRAMS\${APP}.lnk"
  Delete "$INSTDIR\${APP}.exe"
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\THIRD_PARTY_NOTICES.txt"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKCU "${UNINST}"
SectionEnd
