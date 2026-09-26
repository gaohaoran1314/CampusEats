@echo off

rem ================================================================
rem  CampusEats launcher (tier 0)
rem
rem  1. start server if not running (minimized)
rem  2. skip if already running
rem  3. open Edge in app mode: no address bar, no tabs
rem
rem  This file is pure ASCII on purpose. Do NOT add Chinese text here:
rem  cmd.exe on zh-CN Windows reads .bat as GBK and any mixed encoding
rem  breaks the whole file. Keep it ASCII.
rem ================================================================

set "EXE=%~dp0cmake-build-debug\CampusEatsServer.exe"
set "URL=http://127.0.0.1:8080/"

set "EDGE=%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe"
if not exist "%EDGE%" set "EDGE=%ProgramFiles%\Microsoft\Edge\Application\msedge.exe"
if not exist "%EDGE%" set "EDGE=%LocalAppData%\Microsoft\Edge\Application\msedge.exe"

echo.
echo  Checking server...

tasklist /FI "IMAGENAME eq CampusEatsServer.exe" 2>nul | find /I "CampusEatsServer.exe" >nul
if errorlevel 1 goto START_SERVER

echo  Server is already running.
goto OPEN_EDGE

:START_SERVER
if not exist "%EXE%" goto NO_EXE
echo  Starting server (waiting up to 3 seconds)...
start "" /min "%EXE%"
timeout /t 3 /nobreak >nul
goto OPEN_EDGE

:NO_EXE
echo.
echo  [ERROR] Server executable not found:
echo      %EXE%
echo.
echo  Open the cmake-build-debug folder, find CampusEatsServer.exe,
echo  then edit this file with Notepad and fix the EXE= line above.
echo.
pause
exit /b 1

:OPEN_EDGE
if not exist "%EDGE%" goto NO_EDGE
echo  Opening window...
start "" "%EDGE%" --app=%URL%
exit /b 0

:NO_EDGE
echo.
echo  [ERROR] Microsoft Edge not found.
echo  Edit this file with Notepad and fix the EDGE= lines.
echo.
pause
exit /b 1
