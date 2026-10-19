param(
    [Parameter(Mandatory = $true)][string]$Path,
    [switch]$PassThru
)
$ErrorActionPreference = 'Stop'
$imageBytes = @{}
$baseAddress = 0
$ended = $false
foreach ($line in Get-Content -LiteralPath $Path) {
    if (!$line) { continue }
    if ($ended -or $line -notmatch '^:([0-9A-Fa-f]{2})+$') { throw 'Invalid HEX record.' }
    $record = @()
    for ($i = 1; $i -lt $line.Length; $i += 2) {
        $record += [Convert]::ToInt32($line.Substring($i, 2), 16)
    }
    $length = $record[0]
    if ($record.Count -ne $length + 5 -or (($record | Measure-Object -Sum).Sum -band 255) -ne 0) {
        throw 'HEX length or checksum mismatch.'
    }
    $offset = $record[1] * 256 + $record[2]
    switch ($record[3]) {
        0 {
            for ($i = 0; $i -lt $length; ++$i) {
                $address = $baseAddress + $offset + $i
                if (!(($address -ge 0 -and $address -lt 4096) -or $address -eq 0x400e -or $address -eq 0x400f)) {
                    throw ('Unexpected HEX data at 0x{0:X}; only flash and configuration are allowed.' -f $address)
                }
                if ($imageBytes.ContainsKey($address)) { throw 'Overlapping HEX records.' }
                $imageBytes[$address] = $record[4 + $i]
            }
        }
        1 {
            if ($length -ne 0) { throw 'Invalid end record.' }
            $ended = $true
        }
        2 {
            if ($length -ne 2) { throw 'Invalid segment record.' }
            $baseAddress = ($record[4] * 256 + $record[5]) * 16
        }
        4 {
            if ($length -ne 2) { throw 'Invalid linear address record.' }
            $baseAddress = ($record[4] * 256 + $record[5]) * 65536
        }
        default { throw 'Unsupported HEX record type.' }
    }
}
if (!$ended -or !$imageBytes.ContainsKey(0) -or !$imageBytes.ContainsKey(1)) {
    throw 'HEX is incomplete or has no reset vector.'
}
foreach ($address in @($imageBytes.Keys)) {
    $wordAddress = $address -band (-bnot 1)
    if (!$imageBytes.ContainsKey($wordAddress) -or !$imageBytes.ContainsKey($wordAddress + 1)) {
        throw 'Incomplete PIC instruction word.'
    }
    if ($imageBytes[$wordAddress + 1] -gt 0x3f) { throw 'Word exceeds PIC 14-bit width.' }
}
if (!$imageBytes.ContainsKey(0x400e) -or !$imageBytes.ContainsKey(0x400f) -or
    ($imageBytes[0x400e] + 256 * $imageBytes[0x400f]) -ne 0x33c4) {
    throw 'Configuration word must be 0x33C4 for this board.'
}
if ($PassThru) { return $imageBytes }
Write-Output ('Verified {0}: PIC flash bounds, HEX checksums, config 0x33C4, no EEPROM payload.' -f $Path)
