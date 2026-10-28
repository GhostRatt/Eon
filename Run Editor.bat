@echo off
REM Opens the zone editor. Give it a zone file to open a different one, e.g.
REM   "Run Editor.bat" content\zones\outdoors_01.json
cd /d "%~dp0"
if not exist "build\bin\Release\eon_editor.exe" (
  echo eon_editor.exe isn't built yet. Run BUILD.bat first.
  pause
  exit /b 1
)
start "" "build\bin\Release\eon_editor.exe" %*
