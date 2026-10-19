$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$programScript = Join-Path $projectRoot 'scripts/program.ps1'
$testRoot = Join-Path $projectRoot ('build/program tests-' + [Guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $testRoot)
$mockPath = Join-Path $testRoot 'mock-minipro.ps1'
@'
$ErrorActionPreference = 'Stop'
$root = $env:PIC_MINIPRO_TEST_STATE
Add-Content -LiteralPath (Join-Path $root 'calls.jsonl') -Value (ConvertTo-Json -InputObject @($args) -Compress)
$argList = @($args)
if ($argList.Count -lt 7 -or $argList[0] -ne '--infoic' -or $argList[2] -ne '--logicic' -or $argList[4] -ne '-p') {
    throw 'Database options, paths, and device selector must be separate arguments'
}
if (!(Test-Path -LiteralPath $argList[1] -PathType Leaf) -or !(Test-Path -LiteralPath $argList[3] -PathType Leaf)) {
    throw 'Database arguments must name existing files'
}
if ($argList -contains '-V') { Write-Output 'Found TL866CS 03.2.86 (0x256)'; exit 0 }
if ($argList -contains '-D') {
    if ($env:PIC_MINIPRO_TEST_FAIL -eq 'id') { exit 2 }
    exit 0
}
$pageIndex = [Array]::IndexOf($argList, '-c')
$page = if ($pageIndex -ge 0) { $argList[$pageIndex + 1] } else { '' }
$memory = Join-Path $root 'memory.bin'
$data = Join-Path $root 'data.bin'
$configPath = Join-Path $root 'config.conf'
if ($argList -contains '-r') {
    $out = $argList[[Array]::IndexOf($argList, '-r') + 1]
    Copy-Item -LiteralPath $memory -Destination $out
    Copy-Item -LiteralPath $data -Destination ($out + '.eeprom.bin')
    Copy-Item -LiteralPath $configPath -Destination ($out + '.fuses.conf')
    exit 0
}
function Get-Config([string]$path) { Get-Content -LiteralPath $path -Raw | ConvertFrom-StringData }
if ($argList -contains '-m') {
    $file = $argList[[Array]::IndexOf($argList, '-m') + 1]
    if ($env:PIC_MINIPRO_TEST_FAIL -eq 'backup' -and (Split-Path -Leaf $file) -eq 'original.bin') { exit 3 }
    if ($page -eq 'config') {
        if ($env:PIC_MINIPRO_TEST_FAIL -eq 'fuses' -and $argList -contains '--fuses') { exit 4 }
        $actual = Get-Config $configPath
        $expected = Get-Config $file
        $keys = if ($argList -contains '--uid') { @('user_id0','user_id1','user_id2','user_id3') }
                elseif ($argList -contains '--fuses') { @('word1') } else { @('word1','user_id0','user_id1','user_id2','user_id3') }
        foreach ($key in $keys) { if ($actual[$key] -ne $expected[$key]) { exit 5 } }
    } else {
        $source = if ($page -eq 'code') { $memory } else { $data }
        if ((Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath $file).Hash) { exit 6 }
    }
    exit 0
}
if ($argList -contains '-w') {
    $file = $argList[[Array]::IndexOf($argList, '-w') + 1]
    if ($page -eq 'code') {
        if ($env:PIC_MINIPRO_TEST_FAIL -eq 'write') { exit 7 }
        if ($argList -contains '-e') { throw 'Code write unexpectedly skips erase' }
        Copy-Item -LiteralPath $file -Destination $memory -Force
        $blank = [byte[]]::new(256)
        for ($i = 0; $i -lt 256; ++$i) { $blank[$i] = 255 }
        [IO.File]::WriteAllBytes($data, $blank)
        Set-Content -LiteralPath $configPath -Value "word1=0x3fff`nuser_id0=0x3fff`nuser_id1=0x3fff`nuser_id2=0x3fff`nuser_id3=0x3fff"
    } elseif ($page -eq 'data') {
        if ($argList -notcontains '-e') { throw 'Data write would erase flash' }
        Copy-Item -LiteralPath $file -Destination $data -Force
    } else {
        if ($argList -notcontains '-e') { throw 'Config write would erase memory' }
        $actual = Get-Config $configPath
        $source = Get-Config $file
        $keys = if ($argList -contains '--uid') { @('user_id0','user_id1','user_id2','user_id3') } else { @('word1') }
        foreach ($key in $keys) { $actual[$key] = $source[$key] }
        $actual.GetEnumerator() | ForEach-Object { $_.Key + '=' + $_.Value } | Set-Content -LiteralPath $configPath
    }
    exit 0
}
throw 'Unexpected mock command'
'@ | Set-Content -LiteralPath $mockPath -Encoding utf8
function Assert([bool]$Condition, [string]$Message) { if (!$Condition) { throw $Message } }
function New-Fixture([string]$Name) {
    $caseRoot = Join-Path $testRoot $Name
    [void](New-Item -ItemType Directory -Path $caseRoot)
    $memory = [byte[]]::new(4096)
    for ($i = 0; $i -lt 4096; $i += 2) { $memory[$i] = 255; $memory[$i + 1] = 63 }
    $memory[0] = 0x34; $memory[1] = 0x12
    [IO.File]::WriteAllBytes((Join-Path $caseRoot 'memory.bin'), $memory)
    [byte[]]$data = 0..255
    [IO.File]::WriteAllBytes((Join-Path $caseRoot 'data.bin'), $data)
    Set-Content -LiteralPath (Join-Path $caseRoot 'config.conf') -Value "word1=0x3bdc`nuser_id0=0x0001`nuser_id1=0x0002`nuser_id2=0x0003`nuser_id3=0x0004"
    $env:PIC_MINIPRO_TEST_STATE = $caseRoot
    $env:PIC_MINIPRO_TEST_FAIL = ''
    return $caseRoot
}
$previousState = $env:PIC_MINIPRO_TEST_STATE
$previousFailure = $env:PIC_MINIPRO_TEST_FAIL
try {
    foreach ($reset in @($false, $true)) {
        $fixture = New-Fixture ('success-' + $reset)
        $runDirectory = Join-Path $fixture 'backup'
        $firmware = Join-Path $projectRoot ('build/' + $(if ($reset) { 'indicator-only.hex' } else { 'led-controller.hex' }))
        & $programScript -Firmware $firmware -MiniPro $mockPath -BackupDirectory $runDirectory -ResetSettings:$reset 6>$null
        $manifest = Get-Content -LiteralPath (Join-Path $runDirectory 'manifest.json') -Raw | ConvertFrom-Json
        Assert ($manifest.status -eq 'verified') 'Success was not recorded'
        [byte[]]$actualData = [IO.File]::ReadAllBytes((Join-Path $fixture 'data.bin'))
        for ($i = 0; $i -lt 256; ++$i) { Assert ($actualData[$i] -eq $(if ($reset) { 255 } else { $i })) 'EEPROM policy was not applied' }
        [byte[]]$actualFlash = [IO.File]::ReadAllBytes((Join-Path $fixture 'memory.bin'))
        $image = & (Join-Path $projectRoot 'scripts/verify-hex.ps1') -Path $firmware -PassThru
        foreach ($address in $image.Keys) {
            if ($address -lt 4096) { Assert ($actualFlash[$address] -eq $image[$address]) 'Incorrect flash extraction' }
        }
        Assert ($actualFlash[4094] -eq 255 -and $actualFlash[4095] -eq 63) 'Incorrect erased-word padding'
        $config = Get-Content -LiteralPath (Join-Path $fixture 'config.conf') -Raw | ConvertFrom-StringData
        Assert ($config.word1 -eq '0x33c4' -and $config.user_id0 -eq '0x0001' -and $config.user_id3 -eq '0x0004') 'Configuration or user IDs were not preserved'
        $calls = @(Get-Content -LiteralPath (Join-Path $fixture 'calls.jsonl') | ForEach-Object { ,@($_ | ConvertFrom-Json) })
        $writes = @($calls | Where-Object { $_ -contains '-w' })
        Assert ($writes.Count -eq 4) 'Expected flash, EEPROM, user ID, and fuse writes'
        Assert ($writes[0] -notcontains '-e') 'First write must erase'
        foreach ($call in $writes[1..3]) { Assert ($call -contains '-e') 'Later write must skip erase' }
        Assert ($writes[3] -contains '--fuses') 'Configuration must be programmed last'
        foreach ($call in $calls) {
            foreach ($unsafeFlag in @('-x','-y','-v','-u','-s','-S','-i','-I')) {
                Assert ($call -cnotcontains $unsafeFlag) 'Unexpected protection, ID, size, verify, or ICSP override'
            }
        }
        $writeIndex = 0
        while ($calls[$writeIndex] -notcontains '-w') { ++$writeIndex }
        Assert (@($calls[0..($writeIndex - 1)] | Where-Object { $_ -contains '-m' }).Count -eq 3) 'Backup must be verified before writing'
    }
    foreach ($failure in @('id','backup','write','fuses')) {
        $fixture = New-Fixture ('failure-' + $failure)
        $env:PIC_MINIPRO_TEST_FAIL = $failure
        $runDirectory = Join-Path $fixture 'backup'
        $failed = $false
        try { & $programScript -MiniPro $mockPath -BackupDirectory $runDirectory 6>$null }
        catch { $failed = $true }
        Assert $failed ('Did not abort on ' + $failure + ' failure')
        $manifest = Get-Content -LiteralPath (Join-Path $runDirectory 'manifest.json') -Raw | ConvertFrom-Json
        Assert ($manifest.status -eq 'failed') 'Failed run reported success'
        $calls = @(Get-Content -LiteralPath (Join-Path $fixture 'calls.jsonl') | ForEach-Object { ,@($_ | ConvertFrom-Json) })
        $writes = @($calls | Where-Object { $_ -contains '-w' })
        if ($failure -in @('id','backup')) { Assert ($writes.Count -eq 0) 'Preflight failure reached a write' }
        if ($failure -eq 'write') { Assert ($writes.Count -eq 1) 'Programming continued after write failure' }
        if ($failure -eq 'fuses') { Assert ($calls[-1] -contains '--fuses' -and $calls[-1] -contains '-m') 'Explicit fuse verification did not run' }
    }
    $fixture = New-Fixture 'preview'
    & $programScript -MiniPro $mockPath -BackupDirectory (Join-Path $fixture 'backup') -WhatIf 6>$null
    Assert (!(Test-Path -LiteralPath (Join-Path $fixture 'calls.jsonl'))) 'WhatIf contacted the programmer'
    Assert (!(Test-Path -LiteralPath (Join-Path $fixture 'backup'))) 'WhatIf created a backup directory'
    $fixture = New-Fixture 'existing-directory'
    $failed = $false
    try { & $programScript -MiniPro $mockPath -BackupDirectory $fixture 6>$null } catch { $failed = $true }
    Assert $failed 'Existing backup directory was accepted'
    Assert (!(Test-Path -LiteralPath (Join-Path $fixture 'calls.jsonl'))) 'Existing-directory failure contacted the programmer'
    $fixture = New-Fixture 'protected-chip'
    Set-Content -LiteralPath (Join-Path $fixture 'config.conf') -Value "word1=0x3b9c`nuser_id0=0x0001`nuser_id1=0x0002`nuser_id2=0x0003`nuser_id3=0x0004"
    $failed = $false
    try { & $programScript -MiniPro $mockPath -BackupDirectory (Join-Path $fixture 'backup') 6>$null } catch { $failed = $true }
    Assert $failed 'Protected chip was accepted'
    $calls = @(Get-Content -LiteralPath (Join-Path $fixture 'calls.jsonl') | ForEach-Object { ,@($_ | ConvertFrom-Json) })
    Assert (@($calls | Where-Object { $_ -contains '-w' }).Count -eq 0) 'Protected-chip failure reached a write'
    $fixture = New-Fixture 'invalid-hex'
    $invalidHex = Join-Path $fixture 'bad.hex'
    Set-Content -LiteralPath $invalidHex -Value ':00000001FE'
    $failed = $false
    try { & $programScript -Firmware $invalidHex -MiniPro $mockPath -BackupDirectory (Join-Path $fixture 'backup') 6>$null } catch { $failed = $true }
    Assert $failed 'Invalid HEX was accepted'
    Assert (!(Test-Path -LiteralPath (Join-Path $fixture 'calls.jsonl'))) 'Invalid HEX contacted the programmer'
    Write-Output 'PASS: programming orchestration, HEX splitting/padding, backup-before-erase, EEPROM preservation/reset, user IDs, explicit verification, failure stops, and WhatIf (mock MiniPro only).'
} finally {
    $env:PIC_MINIPRO_TEST_STATE = $previousState
    $env:PIC_MINIPRO_TEST_FAIL = $previousFailure
}
