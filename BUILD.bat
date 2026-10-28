@echo off
REM Rebuilds Eon (needs Visual Studio 2022 with "Desktop development with C++").
REM The first build downloads raylib, Jolt Physics, ufbx, Lua and nlohmann/json and takes a few minutes.
REM Everything is also written to build_log.txt (send that file if something goes wrong).
setlocal
cd /d "%~dp0"
set "LOG=%~dp0build_log.txt"
echo Eon build %date% %time% > "%LOG%"
where cmake >nul 2>nul
if errorlevel 1 (
  for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath 2^>nul`) do (
    if exist "%%i\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "PATH=%%i\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"
  )
)
where cmake >nul 2>nul
if errorlevel 1 (
  echo Couldn't find CMake.
  echo Install Visual Studio 2022 Community with the "Desktop development with C++" workload
  echo (it includes CMake), then run BUILD.bat again.
  echo CMake not found >> "%LOG%"
  pause
  exit /b 1
)
where git >nul 2>nul
if errorlevel 1 echo WARNING: git isn't on PATH; CMake needs it to download Jolt, ufbx, Lua and raylib. >> "%LOG%"
echo [1/3] Configuring...
cmake -S . -B build >> "%LOG%" 2>&1
if errorlevel 1 goto failed
echo [2/3] Compiling (the first time takes a few minutes)...
cmake --build build --config Release --parallel >> "%LOG%" 2>&1
if errorlevel 1 goto failed
echo [3/3] Running the tests...
"build\bin\Release\eon_tests.exe" > "%~dp0test_log.txt" 2>&1
set "TESTRESULT=%errorlevel%"
type "%~dp0test_log.txt" >> "%LOG%"
findstr /c:"passed" "%~dp0test_log.txt"
if not "%TESTRESULT%"=="0" (
  echo Some tests failed:
  findstr /b /c:"FAIL" "%~dp0test_log.txt"
)
echo.
echo Built. Start it with "Run Sim.bat" or "Run Editor.bat".
pause
exit /b 0
:failed
echo.
echo The build failed. The errors:
echo ------------------------------------------------------------
findstr /i /r /c:"error [A-Z][A-Z]*[0-9]" /c:"CMake Error" /c:"fatal error" /c:": error" "%LOG%"
echo ------------------------------------------------------------
echo Full log: build_log.txt
pause
exit /b 1
