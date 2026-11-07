@echo off
setlocal
cd /d "%~dp0"
where cmake >nul 2>nul
if errorlevel 1 (
  echo [ERROR] CMake was not found.
  echo Install Visual Studio 2022 Build Tools with the Desktop development with C++ workload,
  echo and make sure CMake is included.
  pause
  exit /b 1
)
cmake -S . -B build -A x64
if errorlevel 1 goto failed
cmake --build build --config Release
if errorlevel 1 goto failed
echo.
echo Build successful: %cd%\build\Release\TetrisVisionPilot.exe
start "" "%cd%\build\Release\TetrisVisionPilot.exe"
exit /b 0
:failed
echo.
echo Build failed. The error is shown above; this window will stay open.
pause
exit /b 1
