; NSIS Alore Install Script
;
; Written by Jukka Lehtosalo
; Originally based on an example written by Joost Verburg
;
; Things to do before building the installer:
;  * Alore must have been built and installed to C:\Alore (./configure; make; 
;    make install)
;  * Documentation must have been built (make doc)


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

Section "Core components" SecBasic
  ; This section is required
  SectionIn RO
  
  SetOutPath "$INSTDIR"
  
  ; Install core files (from C:\Alore).
  File "C:\Alore\alore.exe"
  File "C:\Alore\alorec.exe"
  File /R "C:\Alore\lib"
  File /R "C:\Alore\include"
  File /R "C:\Alore\share"
  ; Install text files
  File "..\..\README.txt"
  File "..\..\LICENSE.txt"
  File "..\..\CHANGELOG.txt"
  File "..\..\CREDITS.txt"
  
  ; Store installation folder
  WriteRegStr HKCU "Software\Alore" "" $INSTDIR
  
  ; Create uninstaller
  WriteUninstaller "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Documentation" SecDocs
  SetOutPath "$INSTDIR\doc"

  ; Install documentation files.  
  File "..\..\doc\html\*.html"
  File "..\..\doc\html\*.css"
SectionEnd

; Descriptions

  ; Language strings
  LangString DESC_SecBasic ${LANG_ENGLISH} \
    "Alore interpreter, type checker and standard library modules."
  LangString DESC_SecDocs ${LANG_ENGLISH} \
    "Alore HTML documentation."

  ; Assign language strings to sections
  !insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecBasic} $(DESC_SecBasic)
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDocs} $(DESC_SecDocs)
  !insertmacro MUI_FUNCTION_DESCRIPTION_END

; Uninstaller Section

Section "Uninstall"
  ; Delete core files
  
  RMDir /r "$INSTDIR\share"
  RMDir /r "$INSTDIR\lib"
  
  Delete "$INSTDIR\include\*.h"
  RMDir "$INSTDIR\include"

  Delete "$INSTDIR\alore*.exe"
  
  Delete "$INSTDIR\README.txt"
  Delete "$INSTDIR\LICENSE.txt"
  Delete "$INSTDIR\CHANGELOG.txt"
  Delete "$INSTDIR\CREDITS.txt"
  
  ; Delete documentation
  
  Delete "$INSTDIR\doc\*.html"
  Delete "$INSTDIR\doc\*.css"
  RMDir "$INSTDIR\doc"

  Delete "$INSTDIR\Uninstall.exe"

  RMDir "$INSTDIR"

  DeleteRegKey /ifempty HKCU "Software\Alore"
SectionEnd