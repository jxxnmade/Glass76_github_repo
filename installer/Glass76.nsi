;------------------------------------------------------------------------------
; Glass76 -- Windows installer
;
; Build with:
;   powershell -ExecutionPolicy Bypass -File installer\build_installer.ps1
;
; or by hand:
;   makensis /DVERSION=1.0.0 installer\Glass76.nsi
;
; Produces a per-machine (elevated) installer that drops the Glass76.vst3
; bundle into the shared VST3 folder, registers with Apps & features, and
; writes an uninstaller. Paths are anchored to this script's own directory,
; so makensis can be run from anywhere.
;------------------------------------------------------------------------------

Unicode true
ManifestDPIAware true

!define NSIDIR "${__FILEDIR__}"
!define ROOT   "${__FILEDIR__}\.."

;--- Inputs, all overridable from the command line with /D -------------------
!ifndef VERSION
  !define VERSION "1.0.0"
!endif
!ifndef VERSION4
  !define VERSION4 "${VERSION}.0"
!endif
!ifndef BUNDLE_DIR
  !define BUNDLE_DIR "${ROOT}\build\VST3\Release\Glass76.vst3"
!endif
!ifndef OUTFILE
  !define OUTFILE "${ROOT}\build\installer\Glass76-${VERSION}-win64.exe"
!endif

!define PRODUCT       "Glass76"
!define PUBLISHER     "Jaxson"
!define DESCRIPTION   "1176-style FET compressor, VST 3"
!define HOMEPAGE      "https://github.com/jxxn/glass76"
!define REGKEY        "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT}"
!define SETTINGSKEY   "Software\${PUBLISHER}\${PRODUCT}"

Name "${PRODUCT} ${VERSION}"
BrandingText "${PRODUCT} ${VERSION}"
OutFile "${OUTFILE}"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUnInstDetails show

; Version resource on the installer .exe itself, so the file properties dialog
; and any SmartScreen prompt have something to show.
VIProductVersion "${VERSION4}"
VIAddVersionKey "ProductName"     "${PRODUCT}"
VIAddVersionKey "FileDescription" "${PRODUCT} installer"
VIAddVersionKey "ProductVersion"  "${VERSION}"
VIAddVersionKey "FileVersion"     "${VERSION4}"
VIAddVersionKey "CompanyName"     "${PUBLISHER}"
VIAddVersionKey "LegalCopyright"  "Copyright (c) 2026 ${PUBLISHER}"

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "WinVer.nsh"
!include "FileFunc.nsh"
!insertmacro GetSize

;--- Pages -------------------------------------------------------------------
!define MUI_ABORTWARNING
!define MUI_ICON   "${NSIDIR}\assets\Glass76.ico"
!define MUI_UNICON "${NSIDIR}\assets\Glass76.ico"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${ROOT}\LICENSE"
!insertmacro MUI_PAGE_COMPONENTS

; The directory page picks the VST3 folder, not a program folder. Hosts scan
; C:\Program Files\Common Files\VST3 by default; anyone using a different one
; already knows they are doing it.
!define MUI_DIRECTORYPAGE_TEXT_TOP \
  "Setup will install the ${PRODUCT} plug-in bundle into the VST3 folder below.$\r$\n$\r$\nLeave it as it is unless your host is configured to scan somewhere else."
!define MUI_DIRECTORYPAGE_TEXT_DESTINATION "VST3 folder"
!insertmacro MUI_PAGE_DIRECTORY

!insertmacro MUI_PAGE_INSTFILES

!define MUI_FINISHPAGE_TITLE "${PRODUCT} is installed"
!define MUI_FINISHPAGE_TEXT \
  "${PRODUCT} was installed to:$\r$\n$INSTDIR\${PRODUCT}.vst3$\r$\n$\r$\nIn FL Studio, open Options > Manage plugins and click Find installed plugins, with Rescan previously verified plugins ticked. Other hosts pick the plug-in up on their next scan."
!define MUI_FINISHPAGE_LINK "Source code and issue tracker"
!define MUI_FINISHPAGE_LINK_LOCATION "${HOMEPAGE}"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

;--- Where the non-plug-in files live ----------------------------------------
; The bundle goes wherever the user pointed the directory page. The uninstaller
; and the docs cannot live inside the bundle -- a host scanning the VST3 folder
; would trip over them -- so they get their own folder under Program Files.
Var DOCDIR

Function .onInit
    ${IfNot} ${RunningX64}
        MessageBox MB_ICONSTOP|MB_OK \
            "${PRODUCT} is 64-bit only, and this is a 32-bit Windows installation."
        Abort
    ${EndIf}
    ${IfNot} ${AtLeastWin10}
        MessageBox MB_ICONEXCLAMATION|MB_YESNO \
            "${PRODUCT} is built and tested against Windows 10 and 11. Continue anyway?" \
            IDYES continue_anyway
        Abort
        continue_anyway:
    ${EndIf}

    SetRegView 64
    StrCpy $DOCDIR "$PROGRAMFILES64\${PUBLISHER}\${PRODUCT}"

    ; Reuse the folder a previous version was installed into, so an upgrade
    ; does not leave a second copy behind in the default location.
    ReadRegStr $0 HKLM "${SETTINGSKEY}" "VST3Path"
    ${If} $0 != ""
    ${AndIf} ${FileExists} "$0\*.*"
        StrCpy $INSTDIR "$0"
    ${Else}
        StrCpy $INSTDIR "$COMMONFILES64\VST3"
    ${EndIf}

    Call CheckPluginLocked
FunctionEnd

; Opening the DLL for append touches nothing but tells us whether a host still
; has it mapped. Installing over a loaded plug-in fails halfway and leaves a
; half-replaced bundle, so it is worth catching before the first file is
; written.
Function CheckPluginLocked
    check_again:
    StrCpy $1 "$INSTDIR\${PRODUCT}.vst3\Contents\x86_64-win\${PRODUCT}.vst3"
    ${IfNot} ${FileExists} "$1"
        Return
    ${EndIf}
    ClearErrors
    FileOpen $2 "$1" a
    ${If} ${Errors}
        MessageBox MB_ICONEXCLAMATION|MB_RETRYCANCEL \
            "${PRODUCT} is currently loaded by another program.$\r$\n$\r$\nClose your DAW (FL Studio, Ableton, Reaper, ...) and any plug-in scanner, then press Retry." \
            IDRETRY check_again
        Abort
    ${Else}
        FileClose $2
    ${EndIf}
FunctionEnd

;--- Sections ----------------------------------------------------------------
Section "VST 3 plug-in" SEC_PLUGIN
    SectionIn RO
    SetOutPath "$INSTDIR"

    ; A stale bundle from an older build can hold resources this version no
    ; longer ships. Replacing rather than merging keeps it honest.
    RMDir /r "$INSTDIR\${PRODUCT}.vst3"

    SetOverwrite on
    File /r "${BUNDLE_DIR}"
    DetailPrint "Installed $INSTDIR\${PRODUCT}.vst3"

    ; Remember the chosen VST3 folder for upgrades and for the uninstaller.
    SetRegView 64
    WriteRegStr HKLM "${SETTINGSKEY}" "VST3Path" "$INSTDIR"
    WriteRegStr HKLM "${SETTINGSKEY}" "Version"  "${VERSION}"
SectionEnd

Section "Documentation and uninstaller" SEC_DOCS
    SectionIn RO
    SetOutPath "$DOCDIR"
    SetOverwrite on
    File "${ROOT}\README.md"
    File "${ROOT}\LICENSE"
    File "${ROOT}\THIRD-PARTY-NOTICES.md"
    File "${ROOT}\CHANGELOG.md"

    WriteUninstaller "$DOCDIR\Uninstall.exe"

    ; Apps & features entry.
    SetRegView 64
    WriteRegStr   HKLM "${REGKEY}" "DisplayName"     "${PRODUCT} (VST 3 plug-in)"
    WriteRegStr   HKLM "${REGKEY}" "DisplayVersion"  "${VERSION}"
    WriteRegStr   HKLM "${REGKEY}" "Publisher"       "${PUBLISHER}"
    WriteRegStr   HKLM "${REGKEY}" "DisplayIcon"     "$DOCDIR\Uninstall.exe"
    WriteRegStr   HKLM "${REGKEY}" "UninstallString" '"$DOCDIR\Uninstall.exe"'
    WriteRegStr   HKLM "${REGKEY}" "QuietUninstallString" '"$DOCDIR\Uninstall.exe" /S'
    WriteRegStr   HKLM "${REGKEY}" "InstallLocation" "$DOCDIR"
    WriteRegStr   HKLM "${REGKEY}" "URLInfoAbout"    "${HOMEPAGE}"
    WriteRegStr   HKLM "${REGKEY}" "HelpLink"        "${HOMEPAGE}/issues"
    WriteRegStr   HKLM "${REGKEY}" "Comments"        "${DESCRIPTION}"
    WriteRegDWORD HKLM "${REGKEY}" "NoModify" 1
    WriteRegDWORD HKLM "${REGKEY}" "NoRepair" 1

    ${GetSize} "$INSTDIR\${PRODUCT}.vst3" "/S=0K" $0 $1 $2
    ${GetSize} "$DOCDIR" "/S=0K" $3 $1 $2
    IntOp $0 $0 + $3
    WriteRegDWORD HKLM "${REGKEY}" "EstimatedSize" "$0"
SectionEnd

Section "Start menu shortcuts" SEC_SHORTCUTS
    CreateDirectory "$SMPROGRAMS\${PRODUCT}"
    CreateShortcut "$SMPROGRAMS\${PRODUCT}\${PRODUCT} manual.lnk"    "$DOCDIR\README.md"
    CreateShortcut "$SMPROGRAMS\${PRODUCT}\Uninstall ${PRODUCT}.lnk" "$DOCDIR\Uninstall.exe"
SectionEnd

LangString DESC_PLUGIN    ${LANG_ENGLISH} "The Glass76.vst3 bundle. This is the plug-in itself."
LangString DESC_DOCS      ${LANG_ENGLISH} "Manual, licence, third-party notices, and the uninstaller."
LangString DESC_SHORTCUTS ${LANG_ENGLISH} "A Start menu folder with the manual and the uninstaller."

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SEC_PLUGIN}    $(DESC_PLUGIN)
    !insertmacro MUI_DESCRIPTION_TEXT ${SEC_DOCS}      $(DESC_DOCS)
    !insertmacro MUI_DESCRIPTION_TEXT ${SEC_SHORTCUTS} $(DESC_SHORTCUTS)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

;--- Uninstall ---------------------------------------------------------------
Function un.onInit
    SetRegView 64
    StrCpy $DOCDIR "$PROGRAMFILES64\${PUBLISHER}\${PRODUCT}"
    ReadRegStr $0 HKLM "${SETTINGSKEY}" "VST3Path"
    ${If} $0 != ""
        StrCpy $INSTDIR "$0"
    ${Else}
        StrCpy $INSTDIR "$COMMONFILES64\VST3"
    ${EndIf}

    ; Same lock check as on install: deleting a mapped DLL silently fails and
    ; the host keeps loading a plug-in the user believes is gone.
    StrCpy $1 "$INSTDIR\${PRODUCT}.vst3\Contents\x86_64-win\${PRODUCT}.vst3"
    ${If} ${FileExists} "$1"
        ClearErrors
        FileOpen $2 "$1" a
        ${If} ${Errors}
            MessageBox MB_ICONEXCLAMATION|MB_OKCANCEL \
                "${PRODUCT} is currently loaded by another program. Close your DAW first, or the plug-in files cannot be removed." \
                IDOK proceed_anyway
            Abort
            proceed_anyway:
        ${Else}
            FileClose $2
        ${EndIf}
    ${EndIf}
FunctionEnd

Section "Uninstall"
    SetRegView 64

    RMDir /r "$INSTDIR\${PRODUCT}.vst3"

    Delete "$DOCDIR\README.md"
    Delete "$DOCDIR\LICENSE"
    Delete "$DOCDIR\THIRD-PARTY-NOTICES.md"
    Delete "$DOCDIR\CHANGELOG.md"
    Delete "$DOCDIR\Uninstall.exe"
    RMDir  "$DOCDIR"
    RMDir  "$PROGRAMFILES64\${PUBLISHER}"

    Delete "$SMPROGRAMS\${PRODUCT}\${PRODUCT} manual.lnk"
    Delete "$SMPROGRAMS\${PRODUCT}\Uninstall ${PRODUCT}.lnk"
    RMDir  "$SMPROGRAMS\${PRODUCT}"

    DeleteRegKey HKLM "${REGKEY}"
    DeleteRegKey HKLM "${SETTINGSKEY}"
    DeleteRegKey /ifempty HKLM "Software\${PUBLISHER}"

    ${If} ${FileExists} "$INSTDIR\${PRODUCT}.vst3\*.*"
        MessageBox MB_ICONEXCLAMATION|MB_OK \
            "Some files could not be removed from$\r$\n$INSTDIR\${PRODUCT}.vst3$\r$\n$\r$\nDelete that folder by hand after restarting."
    ${EndIf}
SectionEnd
