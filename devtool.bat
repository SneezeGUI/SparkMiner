@echo off
REM ============================================================
REM ESP32 DevTool - Universal Build/Flash/Monitor Suite
REM ============================================================
REM Usage:
REM   devtool                          - Interactive menu
REM   devtool build                    - Build firmware
REM   devtool flash                    - Flash firmware
REM   devtool monitor                  - Serial monitor
REM   devtool all                      - Build + Flash + Monitor
REM   devtool release                  - Build all boards
REM   devtool -b cyd-1usb build        - Build specific board
REM   devtool --help                   - Full help
REM ============================================================

cd /d "%~dp0"

REM Check for venv Python first (preferred)
if exist ".venv\Scripts\python.exe" (
    .venv\Scripts\python.exe devtool.py %*
    goto :check_error
)

REM Try system Python
where python >nul 2>nul
if %ERRORLEVEL% equ 0 (
    python devtool.py %*
    goto :check_error
)

REM Try py launcher (Windows Python)
where py >nul 2>nul
if %ERRORLEVEL% equ 0 (
    py devtool.py %*
    goto :check_error
)

echo.
echo ERROR: Python not found!
echo.
echo Install Python from https://www.python.org/downloads/
echo Or create a virtual environment: python -m venv .venv
echo.
pause
exit /b 1

:check_error
if %ERRORLEVEL% neq 0 (
    echo.
    echo Press any key to exit...
    pause >nul
)
