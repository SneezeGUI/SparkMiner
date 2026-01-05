@echo off
REM ============================================================
REM SparkMiner - Freenove ESP32-S3 Display Flash Tool
REM
REM Interactive build and flash tool for the Freenove FNK0104
REM ESP32-S3 with 2.8" ILI9341 display.
REM
REM Usage:
REM   flash-s3-display.bat           - Interactive mode
REM   flash-s3-display.bat --build   - Build only
REM   flash-s3-display.bat --flash   - Flash only
REM   flash-s3-display.bat --all     - Build + Flash + Monitor
REM ============================================================

cd /d "%~dp0"

REM Check for venv Python
if exist ".venv\Scripts\python.exe" (
    .venv\Scripts\python.exe flash-s3-display.py %*
) else (
    REM Fall back to system Python
    python flash-s3-display.py %*
)

if %ERRORLEVEL% neq 0 (
    echo.
    echo Press any key to exit...
    pause >nul
)
