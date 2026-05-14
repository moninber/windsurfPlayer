$ErrorActionPreference = "Stop"

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$CMake = "E:\Qt\Tools\CMake_64\bin\cmake.exe"
$MingwBin = "E:\Qt\Tools\mingw1310_64\bin"

if (!(Test-Path $CMake)) {
    throw "CMake not found: $CMake"
}
if (!(Test-Path (Join-Path $MingwBin "g++.exe"))) {
    throw "MinGW compiler not found: $MingwBin"
}

$env:Path = "$MingwBin;E:\Qt\Tools\Ninja;$env:Path"

& $CMake --preset mingw-release
