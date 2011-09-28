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
  OutFile "alore-setup.exe"

  ; Default installation folder
  InstallDir "C:\Alore"
  
  ; Get installation folder from registry if available
  InstallDirRegKey HKCU Software\Alore ""

  ; Request application privileges for Windows Vista and later
  ; We support installing as a regular user.
  RequestExecutionLevel user
  
  ; Replace the "Nullsoft Install System v..." text in the installer
  BrandingText "Alore (development version)"

; Interface Settings

  !define MUI_ABORTWARNING
  !define MUI_COMPONENTSPAGE_NODESC

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
  
  ; Store installation folder (for current user)
  WriteRegStr HKCU Software\Alore "" "$INSTDIR"
  
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
  
  !define REG_UNINSTALL "Software\Microsoft\Windows\CurrentVersion\Uninstall\Alore"
  
  ; Write the uninstall keys for Windows. Note that we set this up for the
  ; current user only.
  WriteRegStr HKCU "${REG_UNINSTALL}" "DisplayName" "Alore"
  WriteRegStr HKCU "${REG_UNINSTALL}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKCU "${REG_UNINSTALL}" "NoModify" 1
  WriteRegDWORD HKCU "${REG_UNINSTALL}" "NoRepair" 1
  
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

; Uninstaller Section

Section "Uninstall"
  ; Delete registry keys
  
  ; Delete Add/remove programs uninstall key
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Alore"
  ; Delete private registry key
  DeleteRegKey /ifempty HKCU "Software\Alore"
  
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
  
  Delete "$INSTDIR\include\alore\*.h"
  RMDir "$INSTDIR\include\alore"
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