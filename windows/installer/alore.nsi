; NSIS Alore Install Script
;
; Written by Jukka Lehtosalo
;
; Originally based on an example written by Joost Verburg


; Include Modern UI

  !include "MUI2.nsh"

; General

  ; Name and file
  Name "Alore"
  OutFile "Alore-setup.exe"

  ; Default installation folder
  InstallDir "C:\Alore"
  
  ; Get installation folder from registry if available
  InstallDirRegKey HKCU "Software\Alore" ""

  ; Request application privileges for Windows Vista
  RequestExecutionLevel user
  
  ; Do not show the "Nullsoft Install System v..." text.
  BrandingText " "

; Interface Settings

  !define MUI_ABORTWARNING

; Pages

  !insertmacro MUI_PAGE_LICENSE "LICENSE-installer.txt"
  !insertmacro MUI_PAGE_COMPONENTS
  !insertmacro MUI_PAGE_DIRECTORY
  !insertmacro MUI_PAGE_INSTFILES
  
  !insertmacro MUI_UNPAGE_CONFIRM
  !insertmacro MUI_UNPAGE_INSTFILES
  
; Languages
 
  !insertmacro MUI_LANGUAGE "English"

; Installer Sections

Section "Alore Interpreter" SecInterpreter
  ; This section is required
  SectionIn RO
  
  SetOutPath "$INSTDIR"
  
  ; ADD YOUR OWN FILES HERE...
  
  ; Store installation folder
  WriteRegStr HKCU "Software\Alore" "" $INSTDIR
  
  ; Create uninstaller
  WriteUninstaller "$INSTDIR\Uninstall.exe"

SectionEnd

Section "Type Checker" SecChecker

  SetOutPath "$INSTDIR"
  
  ; ADD YOUR OWN FILES HERE...

SectionEnd

; Descriptions

  ; Language strings
  LangString DESC_SecInterpreter ${LANG_ENGLISH} \
    "Alore interpreter and standard library modules."
  LangString DESC_SecChecker ${LANG_ENGLISH} \
    "Alore type checker."

  ; Assign language strings to sections
  !insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecInterpreter} $(DESC_SecInterpreter)
    !insertmacro MUI_DESCRIPTION_TEXT ${SecChecker} $(DESC_SecChecker)
  !insertmacro MUI_FUNCTION_DESCRIPTION_END

; Uninstaller Section

Section "Uninstall"

  ; ADD YOUR OWN FILES HERE...

  Delete "$INSTDIR\Uninstall.exe"

  RMDir "$INSTDIR"

  DeleteRegKey /ifempty HKCU "Software\Alore"

SectionEnd