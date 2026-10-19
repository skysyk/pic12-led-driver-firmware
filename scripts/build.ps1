param(
    [string]$Xc8 = 'xc8-cc',
    [string]$DevicePack = '',
    [switch]$IndicatorOnly
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    if ($Xc8 -eq 'xc8-cc' -and (Test-Path -LiteralPath '.tools/xc8-v4.00/bin/xc8-cc.exe')) {
        $Xc8 = Join-Path $projectRoot '.tools/xc8-v4.00/bin/xc8-cc.exe'
    }
    if (!$DevicePack -and (Test-Path -LiteralPath '.tools/pic-device-pack/xc8')) {
        $DevicePack = Join-Path $projectRoot '.tools/pic-device-pack/xc8'
    }
    if (!(Get-Command $Xc8 -ErrorAction SilentlyContinue)) {
        throw 'XC8 not found. Install MPLAB XC8, or pass -Xc8 with the full xc8-cc.exe path.'
    }
    New-Item -ItemType Directory -Path build -Force | Out-Null
    $image = if ($IndicatorOnly) { 'indicator-only' } else { 'led-controller' }
    $options = @('-mcpu=12F683', '-std=c99', '-O2', '-Ifirmware', '-ginhx32', ('-Wl,-Map=build/' + $image + '.map'))
    if ($DevicePack) { $options += '-mdfp=' + $DevicePack }
    if ($IndicatorOnly) { $options += '-DBOARD_INDICATOR_ONLY=1' }
    $sources = @('firmware/main.c', 'firmware/hw_init.c', 'firmware/buttons.c', 'firmware/dimmer.c')
    if (!$IndicatorOnly) { $sources += 'firmware/storage.c' }
    & $Xc8 @options @sources -o ('build/' + $image + '.elf')
    if ($LASTEXITCODE -ne 0) { throw 'PIC build failed; do not flash an older HEX.' }
    & (Join-Path $PSScriptRoot 'verify-hex.ps1') -Path ('build/' + $image + '.hex')
    Write-Output ('Built build/' + $image + '.hex. Review memory usage and the configuration word before programming.')
} finally { Pop-Location }
