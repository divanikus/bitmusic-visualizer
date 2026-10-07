param(
    [string]$QtRoot = 'C:\Qt\6.11.2\mingw_64',
    [string]$CompilerRoot = 'C:\Qt\Tools\mingw1310_64',
    [string]$CMake = 'C:\Qt\Tools\CMake_64\bin\cmake.exe',
    [string]$NinjaRoot = 'C:\Qt\Tools\Ninja',
    [switch]$Deploy
)
$ErrorActionPreference = 'Stop'
$env:PATH = "$CompilerRoot\bin;$NinjaRoot;$QtRoot\bin;" + $env:PATH
$buildRoot = Join-Path $PSScriptRoot 'build/native'
& $CMake -S $PSScriptRoot -B $buildRoot -G Ninja '-DCMAKE_BUILD_TYPE=Release' "-DCMAKE_PREFIX_PATH=$QtRoot" "-DCMAKE_C_COMPILER=$CompilerRoot/bin/gcc.exe" "-DCMAKE_CXX_COMPILER=$CompilerRoot/bin/g++.exe"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& $CMake --build $buildRoot --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'C++ build failed.' }
if ($Deploy) {
    & "$QtRoot/bin/windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw "$buildRoot/bitmusic_visualizer.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Qt runtime deployment failed.' }
    $licenseRoot = Join-Path $buildRoot 'licenses'
    New-Item -ItemType Directory -Force -Path $licenseRoot | Out-Null
    Copy-Item -LiteralPath "$buildRoot/_deps/gme-src/license.txt" -Destination "$licenseRoot/libgme-LGPL-2.1.txt"
    Copy-Item -LiteralPath "$buildRoot/_deps/zlib-src/LICENSE" -Destination "$licenseRoot/zlib.txt"
    Copy-Item -LiteralPath "$PSScriptRoot/THIRD-PARTY.md" -Destination "$licenseRoot/THIRD-PARTY.md"
}
Write-Output "Built: $buildRoot/bitmusic_visualizer.exe"
