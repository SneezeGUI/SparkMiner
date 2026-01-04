@echo off
REM ============================================================
REM SparkMiner - Simple Flash Script (uses PlatformIO upload)
REM
REM Usage: flash-2usb-simple.bat [build]
REM   build: Optional - rebuilds before flashing
REM ============================================================

set ENV=esp32-2432s028-2usb

echo.
echo ============================================================
echo  SparkMiner Flash Tool - %ENV%
echo ============================================================
echo.

REM Check for build argument
if /i "%~1"=="build" (
    echo [BUILD] Building firmware...
    .venv\Scripts\pio.exe run -e %ENV%
    if %ERRORLEVEL% neq 0 (
        echo [ERROR] Build failed!
        exit /b 1
    )
    echo.
)

REM Upload firmware
echo [FLASH] Uploading firmware...
.venv\Scripts\pio.exe run -e %ENV% -t upload

if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] Upload failed!
    echo.
    echo Troubleshooting:
    echo   1. Hold BOOT button while connecting USB
    echo   2. Try: flash-2usb-simple.bat build
    echo   3. Check USB cable and port
    exit /b 1
)

echo.
echo [SUCCESS] Firmware uploaded!
echo.
echo Starting serial monitor... (Ctrl+C to exit)
echo.

.venv\Scripts\pio.exe device monitor -b 115200
