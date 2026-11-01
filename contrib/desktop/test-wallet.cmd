@echo off
setlocal
set "QT_QPA_PLATFORM=windows"
set "QT_QPA_PLATFORM_PLUGIN_PATH=%~dp0platforms"
set "QT_PLUGIN_PATH=%~dp0"
set "QT_STYLE_OVERRIDE="
set "PATH=%~dp0;%SystemRoot%\System32;%SystemRoot%"
:newfolder
set "BBC_QA_DIR=%TEMP%\Babacoin-UI-QA-%RANDOM%-%RANDOM%"
if exist "%BBC_QA_DIR%" goto newfolder
mkdir "%BBC_QA_DIR%\data" "%BBC_QA_DIR%\gui-settings"
if errorlevel 1 exit /b 1
type nul > "%BBC_QA_DIR%\data\babacoin.conf"
start "" "%~dp0babacoin-qt.exe" -regtest=1 "-datadir=%BBC_QA_DIR%\data" "-conf=%BBC_QA_DIR%\data\babacoin.conf" "-uiconfigdir=%BBC_QA_DIR%\gui-settings" -choosedatadir=0 -connect=0 -listen=0 -dnsseed=0 -discover=0 -upnp=0 -listenonion=0 -maxconnections=0 -enablecoinjoin=0 -windowtitle=Isolated-QA
endlocal
