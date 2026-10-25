# ST-Link & STM32 Automated Target Recovery Tool
param(
    [string]$Target = "stm32g4x.cfg",
    [switch]$HardReset = $false,
    [switch]$EraseAll = $true
)

$ErrorActionPreference = "Continue"

# Locate OpenOCD automatically
$openocd = "$env:LOCALAPPDATA\Arduino15\packages\STMicroelectronics\tools\xpack-openocd\0.12.0-6\bin\openocd.exe"
if (-not (Test-Path $openocd)) {
    $found = Get-Command "openocd.exe" -ErrorAction SilentlyContinue
    if ($found) { $openocd = $found.Source }
}

if (-not (Test-Path $openocd)) {
    Write-Host "ERROR: openocd.exe not found in standard paths!" -ForegroundColor Red
    exit 1
}

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "  STM32 OpenOCD Target Recovery & Diagnostic Tool" -ForegroundColor Cyan
Write-Host "  Target config: $Target" -ForegroundColor White
Write-Host "==========================================================" -ForegroundColor Cyan

$cmds = @("-f","interface/stlink.cfg","-f","target/$Target","-c","adapter speed 100")

if ($HardReset) {
    Write-Host "  Using hardware NRST wire recovery..." -ForegroundColor Yellow
    $cmds += @("-c","reset_config srst_only srst_nogate connect_assert_srst")
} else {
    Write-Host "  Using software SWD recovery..." -ForegroundColor Yellow
    $cmds += @("-c","reset_config none separate")
}

$cmds += @("-c","init")
$cmds += @("-c","reset halt")
$cmds += @("-c","targets")

if ($EraseAll) {
    Write-Host "  Mass erasing internal flash..." -ForegroundColor Yellow
    $cmds += @("-c","flash erase_sector 0 0 last")
}

$cmds += @("-c","exit")

& $openocd $cmds
if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "SUCCESS: Target recovered, halted, and ready!" -ForegroundColor Green
} else {
    Write-Host ""
    Write-Host "FAILED: Check SWD wiring (SWDIO, SWCLK, GND, 3.3V) and try with -HardReset." -ForegroundColor Red
}
