@echo off
REM Runs Eon's core tests (electricity, zones, physics, matter, plants, Jenny's body and hands).
cd /d "%~dp0"
if not exist "build\bin\Release\eon_tests.exe" (
  echo eon_tests.exe isn't built yet. Run BUILD.bat first.
  pause
  exit /b 1
)
"build\bin\Release\eon_tests.exe"
echo.
pause
