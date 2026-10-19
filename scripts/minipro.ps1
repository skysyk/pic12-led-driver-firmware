# Pass explicit database paths so minipro works from any current directory.
$miniproDirectory = Join-Path (Split-Path -Parent $PSScriptRoot) 'build/minipro'
$executable = Join-Path $miniproDirectory 'minipro.exe'
if (!(Test-Path -LiteralPath $executable)) {
    throw 'Build minipro first with scripts/build-minipro.ps1.'
}
& $executable --infoic "$miniproDirectory/infoic.xml" --logicic "$miniproDirectory/logicic.xml" @args
exit $LASTEXITCODE
