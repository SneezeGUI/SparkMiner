<#
.SYNOPSIS
    Flash SparkMiner firmware to ESP32-2432S028-2USB board

.DESCRIPTION
    Builds (optional) and flashes firmware to CYD 2-USB board, then opens serial monitor.

.PARAMETER Build
    Rebuild firmware before flashing

.PARAMETER Port
    Specify COM port (e.g., COM3). Auto-detects if not specified.

.PARAMETER NoBuild
    Skip build even if firmware doesn't exist (use existing)

.PARAMETER NoMonitor
    Don't open serial monitor after flashing

.EXAMPLE
    .\flash-2usb.ps1
    # Auto-detect port, flash existing firmware, open monitor

.EXAMPLE
    .\flash-2usb.ps1 -Build
    # Build first, then flash and monitor

.EXAMPLE
    .\flash-2usb.ps1 -Port COM3 -NoMonitor
    # Flash to COM3, don't open monitor
#>

param(
    [switch]$Build,
    [string]$Port,
    [switch]$NoMonitor
)

$ErrorActionPreference = "Stop"
$ENV = "esp32-2432s028-2usb"
$FIRMWARE = ".pio\build\$ENV\${ENV}_factory.bin"
$BAUD = 115200

function Write-Header {
    Write-Host ""
    Write-Host "============================================================" -ForegroundColor Cyan
    Write-Host "  SparkMiner Flash Tool - $ENV" -ForegroundColor Cyan
    Write-Host "============================================================" -ForegroundColor Cyan
    Write-Host ""
}

function Write-Step($msg) {
    Write-Host "[$((Get-Date).ToString('HH:mm:ss'))] " -NoNewline -ForegroundColor DarkGray
    Write-Host $msg -ForegroundColor Yellow
}

function Write-Success($msg) {
    Write-Host "[$((Get-Date).ToString('HH:mm:ss'))] " -NoNewline -ForegroundColor DarkGray
    Write-Host $msg -ForegroundColor Green
}

function Write-Err($msg) {
    Write-Host "[$((Get-Date).ToString('HH:mm:ss'))] " -NoNewline -ForegroundColor DarkGray
    Write-Host "ERROR: $msg" -ForegroundColor Red
}

Write-Header

# Check if firmware exists or needs building
if (-not (Test-Path $FIRMWARE) -or $Build) {
    Write-Step "Building firmware..."
    & .venv\Scripts\pio.exe run -e $ENV
    if ($LASTEXITCODE -ne 0) {
        Write-Err "Build failed!"
        exit 1
    }
    Write-Success "Build complete!"
    Write-Host ""
}

# Verify firmware exists
if (-not (Test-Path $FIRMWARE)) {
    Write-Err "Firmware not found: $FIRMWARE"
    Write-Host "Run with -Build flag to build first" -ForegroundColor Yellow
    exit 1
}

# Get firmware info
$fwInfo = Get-Item $FIRMWARE
Write-Host "  Firmware: " -NoNewline -ForegroundColor Gray
Write-Host "$($fwInfo.Name)" -ForegroundColor White
Write-Host "  Size:     " -NoNewline -ForegroundColor Gray
Write-Host "$([math]::Round($fwInfo.Length / 1KB, 1)) KB" -ForegroundColor White
Write-Host "  Modified: " -NoNewline -ForegroundColor Gray
Write-Host "$($fwInfo.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))" -ForegroundColor White
Write-Host ""

# Auto-detect port if not specified
if (-not $Port) {
    Write-Step "Detecting ESP32 COM port..."

    $devices = & .venv\Scripts\pio.exe device list --serial --json 2>$null | ConvertFrom-Json
    $esp32Port = $devices | Where-Object {
        $_.description -match "USB|Serial|CP210|CH340|FTDI"
    } | Select-Object -First 1

    if ($esp32Port) {
        $Port = $esp32Port.port
        Write-Host "  Found: " -NoNewline -ForegroundColor Gray
        Write-Host "$Port" -NoNewline -ForegroundColor Green
        Write-Host " ($($esp32Port.description))" -ForegroundColor DarkGray
    } else {
        Write-Err "No ESP32 device found!"
        Write-Host "  Specify port manually: .\flash-2usb.ps1 -Port COM3" -ForegroundColor Yellow
        exit 1
    }
    Write-Host ""
}

# Flash firmware
Write-Step "Flashing to $Port..."
& .venv\Scripts\pio.exe run -e $ENV -t upload --upload-port $Port

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Err "Flash failed!"
    Write-Host ""
    Write-Host "Troubleshooting:" -ForegroundColor Yellow
    Write-Host "  1. Hold BOOT button while connecting USB" -ForegroundColor Gray
    Write-Host "  2. Try a different USB cable (data cable, not charge-only)" -ForegroundColor Gray
    Write-Host "  3. Install CH340 or CP2102 drivers" -ForegroundColor Gray
    Write-Host "  4. Try specifying port: .\flash-2usb.ps1 -Port COM3" -ForegroundColor Gray
    exit 1
}

Write-Host ""
Write-Success "Firmware flashed successfully!"
Write-Host ""

# Open serial monitor
if (-not $NoMonitor) {
    Write-Step "Opening serial monitor (Ctrl+C to exit)..."
    Write-Host ""
    & .venv\Scripts\pio.exe device monitor -b $BAUD -p $Port
}
