; 0xFX NSIS Installer Script — Windows ARM64
; Builds a Windows ARM64 installer from Linux via makensis
;
; Usage: makensis scripts/packaging/0xfx_installer_arm64.nsi

!include "MUI2.nsh"

!define VER "1.3.0"
!define VERFULL "1.3.0.0"

; --- General ---
Name "0xFX v${VER} (ARM64)"
OutFile "../../release/0xFX-${VER}-windows-arm64-setup.exe"
InstallDir "$PROGRAMFILES64\0xFX"
InstallDirRegKey HKLM "Software\0xFX" "InstallDir"
RequestExecutionLevel admin

; --- Version info ---
VIProductVersion "${VERFULL}"
VIAddVersionKey "ProductName" "0xFX"
VIAddVersionKey "CompanyName" "Dan Michael"
VIAddVersionKey "FileDescription" "0xFX Guitar Amp Simulator & Effects Pedalboard (ARM64)"
VIAddVersionKey "FileVersion" "${VER}"
VIAddVersionKey "LegalCopyright" "MIT License"

; --- Interface ---
!define MUI_ABORTWARNING
!define MUI_ICON "..\..\resources\icon\0xfx.ico"
!define MUI_UNICON "..\..\resources\icon\0xfx.ico"

; --- Pages ---
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\..\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; --- Upgrade detection ---
Function .onInit
    ReadRegStr $0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\0xFX" "UninstallString"
    StrCmp $0 "" done

    MessageBox MB_OKCANCEL|MB_ICONINFORMATION \
        "0xFX is already installed.$\n$\nClick OK to uninstall the previous version and continue, or Cancel to abort." \
        IDOK uninst
    Abort

uninst:
    ExecWait '"$0" /S _?=$INSTDIR'
    Delete "$INSTDIR\uninstall.exe"

done:
FunctionEnd

; --- Sections ---

Section "0xFX Standalone (required)" SecMain
    SectionIn RO

    SetOutPath "$INSTDIR"
    File "..\..\build_win_arm64\0xfx_gui.exe"
    File "..\..\resources\icon\0xfx.ico"
    File "..\..\README.md"
    File "..\..\LICENSE"

    ; Factory presets
    SetOutPath "$INSTDIR\presets\factory"
    File /r "..\..\presets\factory\*.*"

    ; Bundled cab impulse responses (public domain)
    SetOutPath "$INSTDIR\resources\ir\bundled"
    File /r "..\..\resources\ir\bundled\*.*"

    ; Start Menu
    CreateDirectory "$SMPROGRAMS\0xFX"
    CreateShortCut "$SMPROGRAMS\0xFX\0xFX.lnk" "$INSTDIR\0xfx_gui.exe" "" "$INSTDIR\0xfx.ico"
    CreateShortCut "$SMPROGRAMS\0xFX\Uninstall.lnk" "$INSTDIR\uninstall.exe"

    ; Desktop shortcut
    CreateShortCut "$DESKTOP\0xFX.lnk" "$INSTDIR\0xfx_gui.exe" "" "$INSTDIR\0xfx.ico"

    ; Registry
    WriteRegStr HKLM "Software\0xFX" "InstallDir" "$INSTDIR"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\0xFX" \
        "DisplayName" "0xFX - Guitar Amp Simulator & Effects Pedalboard (ARM64)"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\0xFX" \
        "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\0xFX" \
        "DisplayVersion" "${VER}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\0xFX" \
        "Publisher" "Dan Michael"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\0xFX" \
        "DisplayIcon" "$INSTDIR\0xfx.ico"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\0xFX" \
        "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\0xFX" \
        "NoRepair" 1

    ; Uninstaller
    WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

Section "CLAP Plugin" SecCLAP
    SetOutPath "$COMMONFILES64\CLAP"
    File "..\..\build_win_arm64\0xFX.clap"
SectionEnd

Section "VST3 Plugin" SecVST3
    SetOutPath "$COMMONFILES64\VST3\0xFX.vst3\Contents\arm64-win"
    File "..\..\build_win_arm64\0xFX.vst3\Contents\arm64-win\0xFX.vst3"
SectionEnd

; --- Descriptions ---
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "0xFX standalone guitar amp simulator with factory presets."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecCLAP} "CLAP plugin for DAWs (installs to Common Files\CLAP)."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecVST3} "VST3 plugin for DAWs (installs to Common Files\VST3)."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; --- Uninstaller ---

Section "Uninstall"
    Delete "$INSTDIR\0xfx_gui.exe"
    Delete "$INSTDIR\0xfx.ico"
    Delete "$INSTDIR\README.md"
    Delete "$INSTDIR\LICENSE"
    Delete "$INSTDIR\uninstall.exe"

    RMDir /r "$INSTDIR\presets"
    RMDir /r "$INSTDIR\recordings"
    RMDir "$INSTDIR"

    ; Plugins
    Delete "$COMMONFILES64\CLAP\0xFX.clap"
    Delete "$COMMONFILES64\VST3\0xFX.vst3\Contents\arm64-win\0xFX.vst3"
    RMDir "$COMMONFILES64\VST3\0xFX.vst3\Contents\arm64-win"
    RMDir "$COMMONFILES64\VST3\0xFX.vst3\Contents"
    RMDir "$COMMONFILES64\VST3\0xFX.vst3"

    ; Shortcuts
    Delete "$SMPROGRAMS\0xFX\0xFX.lnk"
    Delete "$SMPROGRAMS\0xFX\Uninstall.lnk"
    RMDir "$SMPROGRAMS\0xFX"
    Delete "$DESKTOP\0xFX.lnk"

    ; Registry
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\0xFX"
    DeleteRegKey HKLM "Software\0xFX"
SectionEnd
