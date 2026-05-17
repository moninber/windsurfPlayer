$ErrorActionPreference = "Stop"

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$Preset = if (Test-Path (Join-Path $ProjectRoot "CMakeUserPresets.json")) {
    "mingw-release-local"
}
else {
    "mingw-release"
}
$Exe = Join-Path $ProjectRoot "build\$Preset\MediaStudio.exe"

if (!(Test-Path $Exe)) {
    & (Join-Path $PSScriptRoot "build-mingw.ps1")
}

if (!(Test-Path $Exe)) {
    throw "MediaStudio.exe not found: $Exe"
}

& $Exe @args
