<#
.SYNOPSIS
    Flash SparkMiner firmware to an ESP32-S3 DevKitC-1 N16R8 board.

.PARAMETER Build
    Rebuild firmware before flashing.

.PARAMETER Port
    Specify COM port, for example COM40. Auto-detects if not specified.

.PARAMETER NoMonitor
    Do not open serial monitor after flashing.
#>

param(
    [switch]$Build,
    [string]$Port,
    [switch]$NoMonitor
)

$ErrorActionPreference = "Stop"
$ENV_NAME = "esp32-s3-devkit"
$FIRMWARE = ".pio\build\$ENV_NAME\firmware.bin"
$BAUD = 115200

function Write-Header {
    Write-Host ""
    Write-Host "============================================================" -ForegroundColor Cyan
    Write-Host "  SparkMiner Flash Tool - $ENV_NAME / ESP32-S3 N16R8" -ForegroundColor Cyan
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

if (-not (Test-Path $FIRMWARE) -or $Build) {
    Write-Step "Building firmware..."
    & .venv\Scripts\pio.exe run -e $ENV_NAME
    if ($LASTEXITCODE -ne 0) {
        Write-Err "Build failed!"
        exit 1
    }
    Write-Success "Build complete!"
    Write-Host ""
}

if (-not (Test-Path $FIRMWARE)) {
    Write-Err "Firmware not found: $FIRMWARE"
    Write-Host "Run with -Build to build it first." -ForegroundColor Yellow
    exit 1
}

$fwInfo = Get-Item $FIRMWARE
Write-Host "  Firmware: " -NoNewline -ForegroundColor Gray
Write-Host "$($fwInfo.Name)" -ForegroundColor White
Write-Host "  Size:     " -NoNewline -ForegroundColor Gray
Write-Host "$([math]::Round($fwInfo.Length / 1KB, 1)) KB" -ForegroundColor White
Write-Host "  Modified: " -NoNewline -ForegroundColor Gray
Write-Host "$($fwInfo.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))" -ForegroundColor White
Write-Host ""

if (-not $Port) {
    Write-Step "Detecting ESP32-S3 COM port..."
    $devices = & .venv\Scripts\pio.exe device list --serial --json 2>$null | ConvertFrom-Json
    $esp32Port = $devices | Where-Object {
        $_.description -match "USB|Serial|CP210|CH340|FTDI|JTAG"
    } | Select-Object -First 1

    if ($esp32Port) {
        $Port = $esp32Port.port
        Write-Host "  Found: " -NoNewline -ForegroundColor Gray
        Write-Host "$Port" -NoNewline -ForegroundColor Green
        Write-Host " ($($esp32Port.description))" -ForegroundColor DarkGray
    } else {
        Write-Err "No ESP32-S3 serial device found."
        Write-Host "  Specify port manually: .\flash-s3-devkit.ps1 -Port COM40" -ForegroundColor Yellow
        exit 1
    }
    Write-Host ""
}

Write-Step "Flashing to $Port..."
& .venv\Scripts\pio.exe run -e $ENV_NAME -t upload --upload-port $Port

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Err "Flash failed!"
    Write-Host "  Hold BOOT while connecting USB, or pass the exact port with -Port COM40." -ForegroundColor Gray
    exit 1
}

Write-Host ""
Write-Success "Firmware flashed successfully!"
Write-Host ""

if (-not $NoMonitor) {
    Write-Step "Opening serial monitor (Ctrl+C to exit)..."
    Write-Host ""
    & .venv\Scripts\pio.exe device monitor -b $BAUD -p $Port
}
