@echo off
REM ============================================================
REM SparkMiner - Flash ESP32-2432S028-2USB Development Firmware
REM
REM Usage: flash-2usb.bat [COM_PORT]
REM   COM_PORT: Optional COM port (e.g., COM3). Auto-detects if not specified.
REM
REM Examples:
REM   flash-2usb.bat          - Auto-detect port and flash
REM   flash-2usb.bat COM3     - Flash to COM3
REM ============================================================

setlocal enabledelayedexpansion

set ENV=esp32-2432s028-2usb
set FIRMWARE=firmware\dev\%ENV%_factory.bin
set BAUD=921600

REM Check if firmware exists
if not exist "%FIRMWARE%" (
    echo.
    echo [ERROR] Firmware not found: %FIRMWARE%
    echo.
    echo Run build first:
    echo   .venv\Scripts\pio.exe run -e %ENV%
    echo.
    exit /b 1
)

REM Get COM port from argument or auto-detect
if not "%~1"=="" (
    set "COM_PORT=%~1"
    goto :port_ready
)

echo [INFO] Auto-detecting ESP32 COM port...

REM Use platformio to detect
set "COM_PORT="
for /f "tokens=*" %%i in ('.venv\Scripts\pio.exe device list --serial 2^>nul ^| findstr /i "COM"') do (
    for /f "tokens=1" %%j in ("%%i") do (
        if not defined COM_PORT set "COM_PORT=%%j"
    )
)

if not defined COM_PORT (
    echo [ERROR] No ESP32 device found. Please specify COM port:
    echo   flash-2usb.bat COM3
    exit /b 1
)

echo [INFO] Found device on !COM_PORT!

:port_ready

echo.
echo ============================================================
echo  SparkMiner Flash Tool
echo ============================================================
echo  Environment: %ENV%
echo  Firmware:    %FIRMWARE%
echo  Port:        !COM_PORT!
echo  Baud:        %BAUD%
echo ============================================================
echo.

REM Flash using platformio's esptool
echo [FLASH] Flashing firmware to !COM_PORT!...
echo.

.venv\Scripts\python.exe -m esptool --chip esp32 --port !COM_PORT! --baud %BAUD% ^
    --before default_reset --after hard_reset ^
    write_flash -z --flash-mode dio --flash-freq 40m --flash-size detect ^
    0x0 "%FIRMWARE%"

if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] Flash failed!
    echo.
    echo Troubleshooting:
    echo   1. Hold BOOT button while connecting USB
    echo   2. Try a different USB cable
    echo   3. Check if correct COM port
    echo.
    exit /b 1
)

echo.
echo ============================================================
echo [SUCCESS] Firmware flashed successfully!
echo ============================================================
echo.
echo Opening serial monitor in 3 seconds... (Ctrl+C to cancel)
timeout /t 3 /nobreak >nul

REM Open serial monitor
.venv\Scripts\pio.exe device monitor -b 115200 -p !COM_PORT!
