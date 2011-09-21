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

; Include misc headers
  
  ; For ${If} etc.
  !include "LogicLib.nsh"
  ; For ${RefreshShellIcons}
  !include "FileFunc.nsh"

; General

  ; Program name and installer file
  Name "Alore"
  OutFile "Alore-setup.exe"

  ; Default installation folder
  InstallDir "C:\Alore"
  
  ; Get installation folder from registry if available
  InstallDirRegKey HKLM Software\Alore ""

  ; Request application privileges for Windows Vista and later
  RequestExecutionLevel admin
  
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

; Install interpreter, libraries, include files and type checker. Also set up
; file associations and create uninstaller.
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
  WriteRegStr HKLM Software\Alore "" "$INSTDIR"
  
  ; Set file association
  
  ; Define extension .alo
  WriteRegStr HKCR ".alo" "" "Alore.File"
  ; Define user-visible name for Alore files
  WriteRegStr HKCR "Alore.File" "" "Alore File"
  ; Define icon for Alore files
  WriteRegStr HKCR "Alore.File\DefaultIcon" "" "$INSTDIR\alore.exe,0"
  ; Run alore interpreter when an Alore file is double cliecked
  WriteRegStr HKCR "Alore.File\shell\open\command" "" \
      '"$INSTDIR\alore.exe" "%1"'
  ; Refresh icons and make the association active
  ${RefreshShellIcons}
  
  ; Write the uninstall keys for Windows
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Alore" "DisplayName" "Alore"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Alore" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Alore" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Alore" "NoRepair" 1
  
  ; Create uninstaller
  WriteUninstaller "$INSTDIR\Uninstall.exe"
SectionEnd

; Optionally copy documentation
Section "Documentation" SecDocs
  SetOutPath "$INSTDIR\doc"

  ; Install documentation files.  
  File "..\..\doc\html\*.html"
  File "..\..\doc\html\*.css"
SectionEnd

; Optionally create Start menu folder
Section "Start menu folder" SecStartMenu
  CreateDirectory "$SMPROGRAMS\Alore"
  
  ; Create shortcut for uninstaller
  CreateShortCut "$SMPROGRAMS\Alore\Uninstall.lnk" "$INSTDIR\uninstall.exe" "" "$INSTDIR\uninstall.exe" 0
  
  ; Check if documentation section was selected
  SectionGetFlags ${SecDocs} $R0 
  IntOp $R0 $R0 & ${SF_SELECTED} 
  
  ; Only add shortcut to documentation if documentation is installed
  ${If} $R0 == ${SF_SELECTED}
  CreateShortCut "$SMPROGRAMS\Alore\Documentation.lnk" "$INSTDIR\doc\index.html" ""
  ${EndIf}
SectionEnd

; Descriptions

  ; Language strings
  LangString DESC_SecBasic ${LANG_ENGLISH} \
    "Alore interpreter, type checker and standard library modules."
  LangString DESC_SecDocs ${LANG_ENGLISH} \
    "Alore HTML documentation."
  LangString DESC_SecStartMenu ${LANG_ENGLISH} \
    "Create Start menu folder."

  ; Assign language strings to sections
  !insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecBasic} $(DESC_SecBasic)
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDocs} $(DESC_SecDocs)
    !insertmacro MUI_DESCRIPTION_TEXT ${SecStartMenu} $(DESC_SecStartMenu)
  !insertmacro MUI_FUNCTION_DESCRIPTION_END

; Uninstaller Section

Section "Uninstall"
  ; Delete registry keys
  
  ; Delete Add/remove programs uninstall key
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Alore"
  ; Delete private registry key
  DeleteRegKey /ifempty HKLM "Software\Alore"
  
  ; Remove file association-related registry keys
  DeleteRegKey HKCR ".alo"
  DeleteRegKey HKCR "Alore.File"
  DeleteRegKey HKCR "Alore.File\DefaultIcon"
  DeleteRegKey HKCR "Alore.File\shell\open\command"
  ; Refresh icons and make the association removal active
  ${RefreshShellIcons}

  ; Remove shortcuts, if any
  Delete "$SMPROGRAMS\Alore\*.*"

  ; Remove Start menu folder
  RMDir "$SMPROGRAMS\Alore"
  
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
  
  ; Delete documentation files
  
  Delete "$INSTDIR\doc\*.html"
  Delete "$INSTDIR\doc\*.css"
  RMDir "$INSTDIR\doc"

  ; Delete uninstaller
  Delete "$INSTDIR\Uninstall.exe"

  ; Delete install directory (it should be empty now)
  RMDir "$INSTDIR"
SectionEnd