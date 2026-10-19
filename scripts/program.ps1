<#
.SYNOPSIS
Back up, program, and verify a socketed PIC12F683 with open-source MiniPro.
.DESCRIPTION
Preserves EEPROM and user IDs by default. Writes flash, restores EEPROM/user
IDs without another erase, and writes configuration last. Stops on any failure.
Use -WhatIf to validate the HEX and preview without accessing the programmer.
.EXAMPLE
.\scripts\program.ps1 -WhatIf
.EXAMPLE
.\scripts\program.ps1 -Firmware .\build\indicator-only.hex
.EXAMPLE
.\scripts\program.ps1 -ResetSettings
#>
[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
param(
    [string]$Firmware = '',
    [ValidateSet('PIC12F683', 'PIC12F683@SOIC8', 'PIC12F683@DFN8')]
    [string]$Device = 'PIC12F683',
    [switch]$ResetSettings,
    [string]$MiniPro = '',
    [string]$DatabaseDirectory = '',
    [string]$BackupDirectory = ''
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$projectRoot = Split-Path -Parent $PSScriptRoot
if (!$Firmware) { $Firmware = Join-Path $projectRoot 'build/led-controller.hex' }
if (!$DatabaseDirectory) { $DatabaseDirectory = Join-Path $projectRoot 'build/minipro' }
if (!$MiniPro) { $MiniPro = Join-Path $DatabaseDirectory 'minipro.exe' }
if (!$BackupDirectory) {
    $runName = 'program-pic12f683-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 6)
    $BackupDirectory = Join-Path $projectRoot ('backups/' + $runName)
}
$firmwarePath = (Resolve-Path -LiteralPath $Firmware).ProviderPath
$imageBytes = & (Join-Path $PSScriptRoot 'verify-hex.ps1') -Path $firmwarePath -PassThru
$backupPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BackupDirectory)
if (Test-Path -LiteralPath $backupPath) { throw "Backup directory already exists: $backupPath" }
$miniProCommand = Get-Command $MiniPro -ErrorAction Stop
if ($miniProCommand.CommandType -notin @('Application', 'ExternalScript')) {
    throw 'MiniPro must be an executable or script path.'
}
$MiniPro = $miniProCommand.Source
$databaseArgs = @()
foreach ($database in @('infoic', 'logicic')) {
    $databasePath = (Resolve-Path -LiteralPath (Join-Path $DatabaseDirectory ($database + '.xml'))).ProviderPath
    $databaseArgs += @(('--' + $database), $databasePath)
}
$settingsPolicy = if ($ResetSettings) { 'reset EEPROM to erased 0xFF' } else { 'preserve all 256 EEPROM bytes' }
Write-Host "Firmware: $firmwarePath"
Write-Host "Device: $Device (socket/adapter mode; no ICSP)"
Write-Host "EEPROM: $settingsPolicy; user IDs: preserve"
Write-Host "Fresh backup and operation logs: $backupPath"
if (!$PSCmdlet.ShouldProcess($Device, "Back up and verify; erase/write flash; $settingsPolicy; restore user IDs; write config 0x33C4; verify all regions")) {
    return
}

[void](New-Item -ItemType Directory -Path $backupPath)
$operationLog = Join-Path $backupPath 'program.log'
$manifestPath = Join-Path $backupPath 'manifest.json'
$manifest = [ordered]@{
    device = $Device
    firmware = $firmwarePath
    firmware_sha256 = (Get-FileHash -LiteralPath $firmwarePath -Algorithm SHA256).Hash.ToLowerInvariant()
    configuration = '0x33C4'
    eeprom_policy = $settingsPolicy
    started_utc = [DateTime]::UtcNow.ToString('o')
    status = 'preparing'
}
function Save-Manifest {
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8
}
function Invoke-MiniPro {
    param([string[]]$CommandArgs)
    $allArgs = $databaseArgs + @('-p', $Device) + $CommandArgs
    Add-Content -LiteralPath $operationLog -Value ('MiniPro arguments: ' + (ConvertTo-Json -InputObject $allArgs -Compress))
    $output = @(& $MiniPro @allArgs 2>&1)
    $result = $LASTEXITCODE
    foreach ($item in $output) {
        $line = $item.ToString()
        Add-Content -LiteralPath $operationLog -Value $line
        Write-Host $line
    }
    if ($result -ne 0) { throw "MiniPro exited with status $result. See $operationLog" }
    return ($output | ForEach-Object { $_.ToString() })
}
function Read-PicConfiguration {
    param([string]$Path)
    $config = @{}
    foreach ($line in Get-Content -LiteralPath $Path) {
        if (!$line.Trim()) { continue }
        if ($line.Trim() -notmatch '^(word1|user_id[0-3])\s*=\s*0x([0-9a-fA-F]{1,4})$') {
            throw "Unexpected configuration entry in $Path"
        }
        $key = $Matches[1]
        if ($config.ContainsKey($key)) { throw "Duplicate configuration entry: $key" }
        $value = [Convert]::ToInt32($Matches[2], 16)
        if ($value -gt 0x3fff) { throw "Invalid PIC configuration value: $key" }
        $config[$key] = $value
    }
    if ($config.Count -ne 5) { throw 'Backup must contain word1 and four user IDs.' }
    return $config
}
function Check-Length {
    param([string]$Path, [long]$Length)
    if ((Get-Item -LiteralPath $Path).Length -ne $Length) { throw "Unexpected backup size: $Path" }
}

try {
    Save-Manifest
    # Never feed the combined HEX to a code-only write: split off configuration
    # and pad the full 2048-word flash with erased 14-bit words, not zero bytes.
    $flash = [byte[]]::new(4096)
    for ($i = 0; $i -lt $flash.Length; $i += 2) {
        $flash[$i] = 0xff
        $flash[$i + 1] = 0x3f
    }
    foreach ($address in $imageBytes.Keys) {
        if ($address -lt 4096) { $flash[$address] = $imageBytes[$address] }
    }
    $flashPath = Join-Path $backupPath 'firmware.bin'
    $configPath = Join-Path $backupPath 'firmware.fuses.conf'
    $eepromPath = Join-Path $backupPath 'firmware.eeprom.bin'
    [IO.File]::WriteAllBytes($flashPath, $flash)
    Set-Content -LiteralPath $configPath -Value 'word1=0x33c4' -Encoding ascii

    $version = @(Invoke-MiniPro -CommandArgs @('-V'))
    $manifest['programmer'] = $version -join "`n"
    if (($version -join "`n") -notmatch 'Found TL866(CS|A)\s') {
        throw 'This script requires a TL866CS or TL866A programmer.'
    }
    Invoke-MiniPro -CommandArgs @('-D') | Out-Null
    $originalPath = Join-Path $backupPath 'original.bin'
    $originalEeprom = $originalPath + '.eeprom.bin'
    $originalConfig = $originalPath + '.fuses.conf'
    Invoke-MiniPro -CommandArgs @('-r', $originalPath) | Out-Null
    Check-Length -Path $originalPath -Length 4096
    Check-Length -Path $originalEeprom -Length 256
    $originalSettings = Read-PicConfiguration -Path $originalConfig
    if (($originalSettings['word1'] -band 0xc0) -ne 0xc0) {
        throw 'Code/data protection prevents a trustworthy backup. No erase or write was attempted.'
    }
    Invoke-MiniPro -CommandArgs @('-c', 'code', '-m', $originalPath) | Out-Null
    Invoke-MiniPro -CommandArgs @('-c', 'data', '-m', $originalEeprom) | Out-Null
    Invoke-MiniPro -CommandArgs @('-c', 'config', '-m', $originalConfig) | Out-Null
    $manifest['backup_files'] = @(foreach ($file in @($originalPath, $originalEeprom, $originalConfig)) {
        [ordered]@{ name = Split-Path -Leaf $file; sha256 = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
    $manifest['status'] = 'backup verified'
    Save-Manifest

    if ($ResetSettings) {
        $eeprom = [byte[]]::new(256)
        for ($i = 0; $i -lt $eeprom.Length; ++$i) { $eeprom[$i] = 0xff }
        [IO.File]::WriteAllBytes($eepromPath, $eeprom)
    } else {
        Copy-Item -LiteralPath $originalEeprom -Destination $eepromPath
    }
    # Only this first write erases. Subsequent writes must retain flash/data.
    $manifest['status'] = 'programming'
    Save-Manifest
    Invoke-MiniPro -CommandArgs @('-c', 'code', '-w', $flashPath) | Out-Null
    Invoke-MiniPro -CommandArgs @('-c', 'data', '-e', '-w', $eepromPath) | Out-Null
    Invoke-MiniPro -CommandArgs @('-c', 'config', '--uid', '-e', '-w', $originalConfig) | Out-Null
    Invoke-MiniPro -CommandArgs @('-c', 'config', '--fuses', '-e', '-w', $configPath) | Out-Null
    # Use explicit verification: upstream's write_fuses() can print a mismatch
    # without returning failure; verify_fuses() correctly reports a nonzero exit.
    Invoke-MiniPro -CommandArgs @('-c', 'code', '-m', $flashPath) | Out-Null
    Invoke-MiniPro -CommandArgs @('-c', 'data', '-m', $eepromPath) | Out-Null
    Invoke-MiniPro -CommandArgs @('-c', 'config', '--uid', '-m', $originalConfig) | Out-Null
    Invoke-MiniPro -CommandArgs @('-c', 'config', '--fuses', '-m', $configPath) | Out-Null
    $manifest['status'] = 'verified'
    $manifest['completed_utc'] = [DateTime]::UtcNow.ToString('o')
    Save-Manifest
    Write-Host "Programming and verification completed. Backup retained at $backupPath"
} catch {
    $manifest['status'] = 'failed'
    $manifest['error'] = $_.Exception.Message
    Save-Manifest
    throw
}
