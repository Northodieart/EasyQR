Unicode true
RequestExecutionLevel admin
SetCompressor /SOLID lzma
SetCompressorDictSize 32
Name "EasyQR 屏幕二维码识别"
BrandingText "EasyQR · 本机处理，不保存数据"
OutFile "${OUTPUT}"

!ifndef APP_EXE
    !error "APP_EXE is not defined"
!endif
!ifndef OUTPUT
    !error "OUTPUT is not defined"
!endif
!ifndef APP_ICON
    !error "APP_ICON is not defined"
!endif
!ifndef LICENSE_FILE
    !error "LICENSE_FILE is not defined"
!endif

!define APP_NAME "EasyQR"
!define APP_VERSION "1.4.4"
!define PUBLISHER "Northodieart"
!define MAIN_CLASS "EasyQR.MainWindow"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\EasyQR"
!define PRODUCT_KEY "Software\EasyQR"

Var UpdateMode

VIProductVersion "1.4.4.0"
VIAddVersionKey "ProductName" "EasyQR"
VIAddVersionKey "ProductVersion" "${APP_VERSION}"
VIAddVersionKey "FileDescription" "EasyQR 屏幕二维码识别安装程序"
VIAddVersionKey "FileVersion" "${APP_VERSION}"
VIAddVersionKey "CompanyName" "${PUBLISHER}"
VIAddVersionKey "LegalCopyright" "Copyright (c) 2026 Northodieart"
VIAddVersionKey "OriginalFilename" "EasyQR-${APP_VERSION}-x64-setup.exe"

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "WinVer.nsh"
!include "FileFunc.nsh"

!define MUI_ABORTWARNING
!define MUI_ICON "${APP_ICON}"
!define MUI_UNICON "${APP_ICON}"
!define MUI_FINISHPAGE_RUN "$INSTDIR\EasyQR.exe"
!define MUI_FINISHPAGE_RUN_TEXT "运行 EasyQR"
!define MUI_FINISHPAGE_LINK "GitHub：Northodieart/EasyQR"
!define MUI_FINISHPAGE_LINK_LOCATION "https://github.com/Northodieart/EasyQR"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${LICENSE_FILE}"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "SimpChinese"

InstallDir "$PROGRAMFILES64\EasyQR"
InstallDirRegKey HKLM "${PRODUCT_KEY}" "InstallDir"

Function .onInit
    SetRegView 64
    StrCpy $UpdateMode "0"
    ${GetParameters} $R0
    ${GetOptions} $R0 "/EASYQRUPDATE" $R1
    ${IfNot} ${Errors}
        StrCpy $UpdateMode "1"
        ReadRegStr $0 HKLM "${PRODUCT_KEY}" "InstallDir"
        ${If} $0 != ""
            StrCpy $INSTDIR $0
        ${EndIf}
    ${EndIf}

    ${If} $UpdateMode != "1"
        ${IfNot} ${RunningX64}
            MessageBox MB_ICONSTOP "EasyQR 仅支持 64 位 Windows 10 / Windows 11。" /SD IDOK
            Abort
        ${EndIf}
        !ifndef SKIP_OS_CHECK
        ${IfNot} ${AtLeastWin10}
            MessageBox MB_ICONSTOP "EasyQR 需要 Windows 10 或更高版本。" /SD IDOK
            Abort
        ${EndIf}
        !endif
    ${EndIf}

    ${If} $UpdateMode == "1"
        StrCpy $R1 0
        update_close_loop:
            FindWindow $0 "${MAIN_CLASS}" ""
            StrCmp $0 0 install_init_done
            SendMessage $0 0x8016 0 0
            Sleep 300
            IntOp $R1 $R1 + 1
            IntCmp $R1 15 install_init_abort update_close_loop install_init_abort
    ${Else}
        FindWindow $0 "${MAIN_CLASS}" ""
        StrCmp $0 0 install_init_done
        MessageBox MB_ICONQUESTION|MB_YESNO|MB_DEFBUTTON1 \
            "EasyQR 正在运行。安装前需要关闭它，是否现在关闭？" IDNO install_init_abort
        SendMessage $0 0x8016 0 0
        Sleep 800
        FindWindow $0 "${MAIN_CLASS}" ""
        StrCmp $0 0 install_init_done
        MessageBox MB_ICONSTOP "无法关闭正在运行的 EasyQR，请退出后重试。" /SD IDOK
    ${EndIf}

install_init_abort:
    Abort
install_init_done:
FunctionEnd

Function .onInstSuccess
    ${If} $UpdateMode == "1"
        Exec '"$INSTDIR\EasyQR.exe"'
    ${EndIf}
FunctionEnd

Section "EasyQR（必需）" SecCore
    SectionIn RO
    SetShellVarContext all
    SetRegView 64

    SetOutPath "$INSTDIR"
    File "${APP_EXE}"
    File "/oname=LICENSE.txt" "${LICENSE_FILE}"
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    CreateDirectory "$SMPROGRAMS\EasyQR"
    CreateShortCut "$SMPROGRAMS\EasyQR\EasyQR.lnk" "$INSTDIR\EasyQR.exe" "" "$INSTDIR\EasyQR.exe" 0
    CreateShortCut "$SMPROGRAMS\EasyQR\卸载 EasyQR.lnk" "$INSTDIR\Uninstall.exe"

    WriteRegStr HKLM "${PRODUCT_KEY}" "InstallDir" "$INSTDIR"
    WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "EasyQR 屏幕二维码识别"
    WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${APP_VERSION}"
    WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "${PUBLISHER}"
    WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\EasyQR.exe,0"
    WriteRegStr HKLM "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "${UNINST_KEY}" "URLInfoAbout" "https://github.com/Northodieart/EasyQR"
    WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKLM "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
    WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
    WriteRegDWORD HKLM "${UNINST_KEY}" "VersionMajor" 1
    WriteRegDWORD HKLM "${UNINST_KEY}" "VersionMinor" 3

    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    IntFmt $0 "0x%08X" $0
    WriteRegDWORD HKLM "${UNINST_KEY}" "EstimatedSize" "$0"
SectionEnd

Section "创建桌面快捷方式" SecDesktop
    SetShellVarContext all
    ${If} $UpdateMode != "1"
    ${OrIf} ${FileExists} "$DESKTOP\EasyQR.lnk"
        CreateShortCut "$DESKTOP\EasyQR.lnk" "$INSTDIR\EasyQR.exe" "" "$INSTDIR\EasyQR.exe" 0
    ${EndIf}
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecCore} "EasyQR 主程序（必需）。"
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "在桌面创建一个 EasyQR 快捷方式。"
!insertmacro MUI_FUNCTION_DESCRIPTION_END


Function un.onInit
    SetRegView 64
    FindWindow $0 "${MAIN_CLASS}" ""
    StrCmp $0 0 un_init_done
    MessageBox MB_ICONQUESTION|MB_YESNO|MB_DEFBUTTON1 \
        "EasyQR 正在运行。卸载前需要关闭它，是否现在关闭？" IDNO un_init_abort
    SendMessage $0 0x8016 0 0 ; EasyQR quit-for-installer message
    Sleep 800
    FindWindow $0 "${MAIN_CLASS}" ""
    StrCmp $0 0 un_init_done
    MessageBox MB_ICONSTOP "无法关闭正在运行的 EasyQR，请退出后重试。" /SD IDOK
un_init_abort:
    Abort
un_init_done:
FunctionEnd

Section "Uninstall"
    SetShellVarContext all
    SetRegView 64

    Delete "$DESKTOP\EasyQR.lnk"
    Delete "$SMPROGRAMS\EasyQR\EasyQR.lnk"
    Delete "$SMPROGRAMS\EasyQR\卸载 EasyQR.lnk"
    RMDir "$SMPROGRAMS\EasyQR"

    Delete "$INSTDIR\EasyQR.exe"
    Delete "$INSTDIR\LICENSE.txt"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"

    DeleteRegKey HKLM "${UNINST_KEY}"
    DeleteRegKey HKLM "${PRODUCT_KEY}"
    DeleteRegKey HKCU "${PRODUCT_KEY}"
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "EasyQR"

    ; The app itself never writes scan data to these locations. Cleanup is defense in depth.
    SetShellVarContext current
    RMDir /r "$APPDATA\EasyQR"
    RMDir /r "$LOCALAPPDATA\EasyQR"
    RMDir /r "$TEMP\EasyQR"
SectionEnd
