@echo off
setlocal
REM Ubuntu must be installed in WSL and integrated with Docker Desktop.
wsl.exe --distribution Ubuntu --cd "%~dp0." --exec bash ./run-windows.sh
if errorlevel 1 (
    echo.
    echo CenterDisplay did not start. See the error above and README.md.
    pause
)
