@echo off
REM Starts Jenny's world (Waiting Area + Test Zones). Extra options go straight to eon_sim, e.g.
REM   "Run Sim.bat" content\zones\outdoors_01.json      or      "Run Sim.bat" --fbx "D:\Models\Primrose.fbx"
cd /d "%~dp0"
if not exist "build\bin\Release\eon_sim.exe" (
  echo eon_sim.exe isn't built yet. Run BUILD.bat first.
  pause
  exit /b 1
)
start "" "build\bin\Release\eon_sim.exe" %*
