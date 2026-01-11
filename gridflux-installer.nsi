; GridFlux Windows Installer Script
; NSIS Modern User Interface

!include "MUI2.nsh"
!include "FileFunc.nsh"

; General Configuration
Name "GridFlux Window Manager"
OutFile "GridFlux-Installer-2.0.0.exe"
InstallDir "$PROGRAMFILES64\GridFlux"
InstallDirRegKey HKLM "Software\GridFlux" "Install_Dir"
RequestExecutionLevel admin

; Version Information
VIProductVersion "2.0.0.0"
VIAddVersionKey "ProductName" "GridFlux Window Manager"
VIAddVersionKey "CompanyName" "GridFlux Project"
VIAddVersionKey "LegalCopyright" "Copyright (C) 2025 GridFlux Project"
VIAddVersionKey "FileDescription" "GridFlux Window Manager Installer"
VIAddVersionKey "FileVersion" "2.0.0"

; MUI Settings
!define MUI_ABORTWARNING
!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"
!define MUI_HEADERIMAGE
!define MUI_FINISHPAGE_SHOWREADME ""
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Open GridFlux Control Panel"
!define MUI_FINISHPAGE_SHOWREADME_FUNCTION LaunchGUI

; Pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Languages
!insertmacro MUI_LANGUAGE "English"

; Custom functions
Function LaunchGridFlux
    Exec 'wscript.exe "$INSTDIR\gridflux-launcher.vbs"'
FunctionEnd

Function LaunchGUI
    ; Give the daemon a moment to start
    Sleep 1000
    Exec '"$INSTDIR\gridflux-gui.exe"'
FunctionEnd

; Installer Sections
Section "GridFlux Core" SecCore
    SectionIn RO  ; Required section
    
    SetOutPath "$INSTDIR"
    
    ; Install main executables
    File "build\gridflux.exe"
    File "build\gridflux-cli.exe"
    File "build\gridflux-gui.exe"
    
    ; Install background launcher script
    File "gridflux-launcher.vbs"
    
    ; Install icons
    SetOutPath "$INSTDIR\icons"
    File "icons\gridflux-16.png"
    File "icons\gridflux-32.png"
    File "icons\gridflux-48.png"
    File /nonfatal "icons\gridflux.ico"
    
    ; Install required DLLs (GTK4 and dependencies)
    SetOutPath "$INSTDIR\bin"
    File /nonfatal /r "C:\msys64\mingw64\bin\*.dll"
    
    ; Create default config directory
    SetOutPath "$APPDATA\GridFlux"
    File /oname=config.json "config.default.json"
    
    ; Write registry keys
    WriteRegStr HKLM "Software\GridFlux" "Install_Dir" "$INSTDIR"
    WriteRegStr HKLM "Software\GridFlux" "Version" "2.0.0"
    
    ; Create uninstaller
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    
    ; Add to Programs and Features
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\GridFlux" "DisplayName" "GridFlux Window Manager"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\GridFlux" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\GridFlux" "DisplayIcon" "$INSTDIR\icons\gridflux.ico"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\GridFlux" "Publisher" "GridFlux Project"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\GridFlux" "DisplayVersion" "2.0.0"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\GridFlux" "URLInfoAbout" "https://github.com/arxngr/gridflux"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\GridFlux" "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\GridFlux" "NoRepair" 1
    
    ; Calculate installed size
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    IntFmt $0 "0x%08X" $0
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\GridFlux" "EstimatedSize" "$0"

    ExecShell "" "wscript.exe" '"$INSTDIR\gridflux-launcher.vbs"' SW_HIDE
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Run" \
    "GridFlux" '"wscript.exe" "$INSTDIR\gridflux-launcher.vbs"'


SectionEnd

Section "Desktop Shortcut" SecDesktop
    CreateShortcut "$DESKTOP\GridFlux Control Panel.lnk" "$INSTDIR\gridflux-gui.exe" "" "$INSTDIR\icons\gridflux.ico"
SectionEnd

Section "Start Menu Shortcuts" SecStartMenu
    CreateDirectory "$SMPROGRAMS\GridFlux"
    CreateShortcut "$SMPROGRAMS\GridFlux\GridFlux Control Panel.lnk" "$INSTDIR\gridflux-gui.exe" "" "$INSTDIR\icons\gridflux.ico"
    CreateShortcut "$SMPROGRAMS\GridFlux\GridFlux CLI.lnk" "$INSTDIR\gridflux-cli.exe" "" "$INSTDIR\icons\gridflux.ico"
    CreateShortcut "$SMPROGRAMS\GridFlux\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

; Section Descriptions
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecCore} "GridFlux core components (required, includes auto-start on login)"
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "Create desktop shortcut for GridFlux Control Panel"
    !insertmacro MUI_DESCRIPTION_TEXT ${SecStartMenu} "Create Start Menu shortcuts"
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; Uninstaller Section
Section "Uninstall"
    ; Stop GridFlux if running
    ExecWait 'taskkill /F /IM gridflux.exe'
    ExecWait 'taskkill /F /IM gridflux-gui.exe'
    
    ; Remove registry keys
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\GridFlux"
    DeleteRegKey HKLM "Software\GridFlux"
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "GridFlux"
    
    ; Remove files and directories
    Delete "$INSTDIR\gridflux.exe"
    Delete "$INSTDIR\gridflux-cli.exe"
    Delete "$INSTDIR\gridflux-gui.exe"
    Delete "$INSTDIR\gridflux-launcher.vbs"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir /r "$INSTDIR\icons"
    RMDir /r "$INSTDIR\bin"
    RMDir "$INSTDIR"
    
    ; Remove shortcuts
    Delete "$DESKTOP\GridFlux Control Panel.lnk"
    RMDir /r "$SMPROGRAMS\GridFlux"
    
    ; Ask to remove config (optional)
    MessageBox MB_YESNO "Do you want to remove configuration files?" IDNO NoDelete
        RMDir /r "$APPDATA\GridFlux"
    NoDelete:
SectionEnd