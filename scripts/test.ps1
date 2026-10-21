param([string]$Compiler = '')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    New-Item -ItemType Directory -Path build -Force | Out-Null
    $sources = @('firmware/buttons.c', 'firmware/dimmer.c', 'firmware/storage.c', 'tests/test_firmware.c')
    $options = @('-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic', '-Ifirmware', '-DFIRMWARE_TEST', '-O1', '-g', '-UNDEBUG')
    if (!$Compiler) {
        $bundled = Join-Path $projectRoot '.tools/zig-x86_64-windows-0.15.2/zig.exe'
        if (Test-Path -LiteralPath $bundled) { $Compiler = $bundled }
        else {
            foreach ($name in @('clang', 'gcc', 'zig')) {
                $found = Get-Command $name -ErrorAction SilentlyContinue
                if ($found) { $Compiler = $found.Source; break }
            }
        }
    }
    if (!$Compiler) { throw 'Set -Compiler to gcc, clang, or zig.' }
    $prefixArgs = @()
    if ((Split-Path -Leaf $Compiler) -match '^zig(\.exe)?$') {
        $env:ZIG_GLOBAL_CACHE_DIR = Join-Path $projectRoot 'build/zig-global-cache'
        $env:ZIG_LOCAL_CACHE_DIR = Join-Path $projectRoot 'build/zig-local-cache'
        $prefixArgs = @('cc')
    }
    & $Compiler @prefixArgs @options @sources -o build/test_firmware.exe
    if ($LASTEXITCODE -ne 0) { throw 'Host compilation failed.' }
    & './build/test_firmware.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Firmware tests failed.' }
    & $Compiler @prefixArgs @options firmware/dimmer.c firmware/storage.c tests/test_storage.c -o build/test_storage.exe
    if ($LASTEXITCODE -ne 0) { throw 'Persistent storage tests compilation failed.' }
    & './build/test_storage.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Persistent storage tests failed.' }
    & $Compiler @prefixArgs @options '-Itests/fake_pic' '-Wno-unknown-pragmas' tests/test_hw.c -o build/test_hw.exe
    if ($LASTEXITCODE -ne 0) { throw 'Hardware-register model compilation failed.' }
    & './build/test_hw.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Hardware-register model tests failed.' }
    & $Compiler @prefixArgs @options '-Itests/fake_pic' '-Wno-unknown-pragmas' firmware/buttons.c firmware/dimmer.c firmware/storage.c tests/test_main.c -o build/test_main.exe
    if ($LASTEXITCODE -ne 0) { throw 'Main-loop integration compilation failed.' }
    & './build/test_main.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Main-loop integration tests failed.' }
} finally { Pop-Location }
