@echo off
REM Rebuilds Eon (needs Visual Studio 2022 with "Desktop development with C++", which includes CMake and Git).
REM The first build downloads raylib, Jolt Physics, ufbx, Lua and nlohmann/json and takes a few minutes.
REM Everything is also written to build_log.txt - send that if something goes wrong.
REM (No parenthesised blocks here on purpose: "Program Files (x86)" inside one breaks cmd.)
setlocal
cd /d "%~dp0"
set "LOG=%~dp0build_log.txt"
echo Eon build %date% %time% > "%LOG%"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSDIR="
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSDIR=%%i"
echo Visual Studio: %VSDIR% >> "%LOG%"

where cmake >nul 2>nul
if not errorlevel 1 goto havecmake
if not defined VSDIR goto nocmake
set "VSCMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
if exist "%VSCMAKE%\cmake.exe" set "PATH=%VSCMAKE%;%PATH%"
where cmake >nul 2>nul
if errorlevel 1 goto nocmake
:havecmake

where git >nul 2>nul
if not errorlevel 1 goto havegit
if not defined VSDIR goto nogit
set "VSGIT=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd"
if exist "%VSGIT%\git.exe" set "PATH=%VSGIT%;%PATH%"
where git >nul 2>nul
if errorlevel 1 goto nogit
:havegit

echo [1/3] Configuring...
cmake -S . -B build >> "%LOG%" 2>&1
if errorlevel 1 goto failed
echo [2/3] Compiling (the first time takes a few minutes)...
cmake --build build --config Release --parallel >> "%LOG%" 2>&1
if errorlevel 1 goto failed
echo [3/3] Running the tests...
"build\bin\Release\eon_tests.exe" > "%~dp0test_log.txt" 2>&1
type "%~dp0test_log.txt" >> "%LOG%"
findstr /c:"passed" "%~dp0test_log.txt"
findstr /b /c:"FAIL" "%~dp0test_log.txt"
echo.
echo Built. Start it with "Run Sim.bat" or "Run Editor.bat".
pause
exit /b 0

:nocmake
echo Couldn't find CMake.
echo Install Visual Studio 2022 Community with the "Desktop development with C++" workload - it includes CMake - then run BUILD.bat again.
echo CMake not found >> "%LOG%"
pause
exit /b 1

:nogit
echo Couldn't find Git - CMake needs it to download Jolt, ufbx, Lua and raylib.
echo Install Git for Windows from https://git-scm.com/download/win and run BUILD.bat again.
echo Git not found >> "%LOG%"
pause
exit /b 1

:failed
echo.
echo The build failed. The errors:
echo ------------------------------------------------------------
findstr /i /r /c:"error [A-Z][A-Z]*[0-9]" /c:"CMake Error" /c:"fatal error" /c:": error" "%LOG%"
echo ------------------------------------------------------------
echo Full log: build_log.txt
pause
exit /b 1
