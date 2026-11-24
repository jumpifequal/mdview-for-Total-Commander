@echo off
setlocal
set "ACTION=Publish"
set "XP_SWITCH="
if /i "%~1"=="--check" set "ACTION=Check"
if /i "%~1"=="--package" set "ACTION=Package"
if /i "%~1"=="--package-xp" (
    set "ACTION=Package"
    set "XP_SWITCH=-IncludeXP"
)
if not "%~2"=="" goto usage
if not "%~1"=="" if "%ACTION%"=="Publish" goto usage
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\mdview-release.ps1" -Action %ACTION% %XP_SWITCH%
set "RESULT=%ERRORLEVEL%"
if not "%RESULT%"=="0" (
    echo.
    echo MDView %ACTION% failed. See the error above.
)
if "%~1"=="" pause
exit /b %RESULT%

:usage
echo Usage: publishme.bat [--check ^| --package ^| --package-xp]
exit /b 2
