;
; This is the installation script for Vitrite. Hopefully, I won't screw up again and delete it.
;

Name "Vitrite"

; Filename of the produced installer
OutFile "VitriteInstall.exe"

; Allow 64-bit installs and registry view selection
!include "x64.nsh"

; Make sure the installer isn't corrupt
CRCCheck on

LicenseText "You must agree to this license before installing Vitrite:"
LicenseData "instgpl.txt"

RequestExecutionLevel admin

; Use the Windows' colorscheme
InstallColors /windows

; Fancy up our progress bar
InstProgressFlags smooth

DirShow show 	; (make this hide to not let the user change it)
DirText "Select the directory to install Vitrite into:"

UninstallText "This will uninstall Vitrite from your system"

Function .onInit
  SetShellVarContext all
  ${If} ${RunningX64}
    SetRegView 64
    StrCpy $INSTDIR "$PROGRAMFILES64\Tiny Utilities\Vitrite"
  ${Else}
    SetRegView 32
    StrCpy $INSTDIR "$PROGRAMFILES\Tiny Utilities\Vitrite"
  ${EndIf}
FunctionEnd

Section "" ; (default section)
  SetOutPath "$INSTDIR"

  ; Copy files
  ; Include both variants if present; pick at runtime.
  File /nonfatal /oname=Vitrite.x86.exe "Vitrite.exe"
  File /nonfatal /oname=Vitrite.x64.exe "Vitrite.x64.exe"
  File /nonfatal /oname=Vitrite.x64.exe "Vitrite64.exe"
  File "ReadMe.txt"
  File "GPL.txt"
  File /nonfatal /oname=README.en.txt "..\..\README"
  File /nonfatal /oname=README.zh-CN.md "..\..\README.zh-CN.md"

  ${If} ${RunningX64}
    SetRegView 64
    IfFileExists "$INSTDIR\Vitrite.x64.exe" 0 +3
      Rename "$INSTDIR\Vitrite.x64.exe" "$INSTDIR\Vitrite.exe"
      Goto +2
    IfFileExists "$INSTDIR\Vitrite.x86.exe" 0 NoBinary
      Rename "$INSTDIR\Vitrite.x86.exe" "$INSTDIR\Vitrite.exe"
  ${Else}
    SetRegView 32
    IfFileExists "$INSTDIR\Vitrite.x86.exe" 0 NoBinary
      Rename "$INSTDIR\Vitrite.x86.exe" "$INSTDIR\Vitrite.exe"
  ${EndIf}

  Delete "$INSTDIR\Vitrite.x86.exe"
  Delete "$INSTDIR\Vitrite.x64.exe"

  ; Write registry entries
  WriteRegStr HKEY_LOCAL_MACHINE "SOFTWARE\Tiny Utilities\Vitrite" "InstallDir" "$INSTDIR"
  WriteRegStr HKEY_LOCAL_MACHINE "Software\Microsoft\Windows\CurrentVersion\Uninstall\Vitrite" "DisplayName" "Vitrite"
  WriteRegStr HKEY_LOCAL_MACHINE "Software\Microsoft\Windows\CurrentVersion\Uninstall\Vitrite" "UninstallString" '"$INSTDIR\uninst.exe"'
  WriteRegStr HKEY_LOCAL_MACHINE "Software\Microsoft\Windows\CurrentVersion\Uninstall\Vitrite" "InstallLocation" "$INSTDIR"

  ; Create Start Menu shortcuts
  CreateDirectory "$SMPROGRAMS\Tiny Utilities\Vitrite"
  CreateShortCut "$SMPROGRAMS\Tiny Utilities\Vitrite\Vitrite.lnk" "$INSTDIR\Vitrite.exe" 
  CreateShortCut "$SMPROGRAMS\Tiny Utilities\Vitrite\Uninstall.lnk" "$INSTDIR\uninst.exe"

  ; Ask about running on startup
  MessageBox MB_ICONQUESTION|MB_YESNO "Do you want Vitrite to run when Windows starts?" IDNO NoStartup
  CreateShortCut "$SMSTARTUP\Vitrite.lnk" "$INSTDIR\Vitrite.exe"
  NoStartup:

  ; Ask about running Vitrite now
  MessageBox MB_ICONQUESTION|MB_YESNO "Do you want to run Vitrite now?" IDNO NoRun
  Exec "$INSTDIR\Vitrite.exe"
  NoRun:

  ; Ask about viewing the ReadMe
  MessageBox MB_ICONQUESTION|MB_YESNO "Do you want to view the ReadMe now?" IDNO NoView
  Exec "$WINDIR\notepad.exe $INSTDIR\ReadMe.txt"
  NoView:

  ; Create uninstaller
  WriteUninstaller "$INSTDIR\uninst.exe"
  
  Goto End
  
  ; Bailout point
  Cancelled:
  Quit

  End:
SectionEnd ; end of default section

Section Uninstall
  ${If} ${RunningX64}
    SetRegView 64
  ${Else}
    SetRegView 32
  ${EndIf}

  Delete "$INSTDIR\*"
  Delete "$INSTDIR\uninst.exe"
  Delete "$SMPROGRAMS\Tiny Utilities\Vitrite\*"
  Delete "$SMSTARTUP\Vitrite.lnk"
  RMDir "$SMPROGRAMS\Tiny Utilities\Vitrite"
  IfFileExists "$SMPROGRAMS\Tiny Utilities\*\" DontDelete
  RMDIR "$SMPROGRAMS\Tiny Utilities"
  DontDelete:

  DeleteRegKey HKEY_LOCAL_MACHINE "SOFTWARE\Tiny Utilities\Vitrite"
  DeleteRegKey HKEY_LOCAL_MACHINE "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Vitrite"
  RMDir "$INSTDIR"
SectionEnd ; end of uninstall section

NoBinary:
  MessageBox MB_ICONSTOP|MB_OK "Vitrite binary not found in installer (expected Vitrite.exe for x86 and/or Vitrite.x64.exe/Vitrite64.exe for x64)."
  Quit

