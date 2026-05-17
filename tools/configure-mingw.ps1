$ErrorActionPreference = "Stop"

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$Preset = if (Test-Path (Join-Path $ProjectRoot "CMakeUserPresets.json")) {
    "mingw-release-local"
}
else {
    "mingw-release"
}

Push-Location $ProjectRoot
try {
    & cmake --preset $Preset
}
finally {
    Pop-Location
}
