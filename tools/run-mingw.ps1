$ErrorActionPreference = "Stop"

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$Exe = Join-Path $ProjectRoot "build\mingw-release\MediaStudio.exe"

if (!(Test-Path $Exe)) {
    & (Join-Path $PSScriptRoot "build-mingw.ps1")
}

if (!(Test-Path $Exe)) {
    throw "MediaStudio.exe not found: $Exe"
}

$RuntimePaths = @(
    "E:\Qt\Tools\mingw1310_64\bin",
    "E:\Qt\6.11.0\mingw_64\bin",
    "E:\ffmpeg\bin",
    "E:\OpenGl\glew-2.3.1-win32\glew-2.3.1\bin\Release\x64"
)

$ExistingRuntimePaths = $RuntimePaths | Where-Object { Test-Path $_ }
$env:Path = ($ExistingRuntimePaths -join ";") + ";$env:Path"

& $Exe @args
