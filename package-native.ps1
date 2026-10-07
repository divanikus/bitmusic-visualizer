param(
    [string]$QtRoot = 'C:\Qt\6.11.2\mingw_64',
    [string]$CompilerRoot = 'C:\Qt\Tools\mingw1310_64',
    [string]$CMake = 'C:\Qt\Tools\CMake_64\bin\cmake.exe',
    [string]$NinjaRoot = 'C:\Qt\Tools\Ninja',
    [string]$OutputPath = ''
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$savedPath = $env:PATH
try {
    $env:PATH = "$CompilerRoot\bin;$NinjaRoot;$QtRoot\bin;" + $savedPath
    $buildRoot = Join-Path $PSScriptRoot 'build/portable-release'
    $versionMatch = [regex]::Match((Get-Content -LiteralPath "$PSScriptRoot/CMakeLists.txt" -Raw), 'project\(BitMusicVisualizer VERSION ([0-9.]+)')
    if (!$versionMatch.Success) { throw 'Cannot read the application version from CMakeLists.txt.' }
    $version = $versionMatch.Groups[1].Value
    $packageName = "BitMusicVisualizer-$version-windows-x64"
    if (!$OutputPath) { $OutputPath = Join-Path $PSScriptRoot "dist/$packageName.zip" }
    $OutputPath = [IO.Path]::GetFullPath($OutputPath)
    if (Test-Path -LiteralPath $OutputPath) { throw "Archive already exists: $OutputPath. Choose another -OutputPath." }
    & $CMake -S $PSScriptRoot -B $buildRoot -G Ninja '-DCMAKE_BUILD_TYPE=Release' '-DBITMUSIC_SHARED_GME=ON' `
        '-DCMAKE_CXX_FLAGS=' '-DCMAKE_C_FLAGS=' '-DCMAKE_EXE_LINKER_FLAGS=-s' '-DCMAKE_SHARED_LINKER_FLAGS=-s' `
        "-DCMAKE_PREFIX_PATH=$QtRoot" "-DCMAKE_C_COMPILER=$CompilerRoot/bin/gcc.exe" "-DCMAKE_CXX_COMPILER=$CompilerRoot/bin/g++.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Portable CMake configuration failed.' }
    & $CMake --build $buildRoot --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Portable build failed.' }

    # Start from an empty directory, never from an existing deployment or the music collection.
    $stageParent = Join-Path $PSScriptRoot ('build/pkg-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
    $stageRoot = Join-Path $stageParent 'BitMusicVisualizer'
    New-Item -ItemType Directory -Path $stageRoot -Force | Out-Null
    Copy-Item -LiteralPath "$buildRoot/bitmusic_visualizer.exe","$buildRoot/libgme.dll" -Destination $stageRoot
    # QAudioSink uses Qt's native Windows audio device implementation. No video,
    # QMediaPlayer, SVG assets or network features are used by this application.
    & "$QtRoot/bin/windeployqt.exe" --release --compiler-runtime --no-translations --no-ffmpeg `
        --no-system-d3d-compiler --no-system-dxc-compiler --no-opengl-sw `
        --skip-plugin-types multimedia,imageformats,iconengines,networkinformation,tls,generic,styles,qmltooling `
        "$stageRoot/bitmusic_visualizer.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Qt runtime deployment failed.' }
    # Allows decoder/CPU checks on CI hosts without an interactive desktop.
    Copy-Item -LiteralPath "$QtRoot/plugins/platforms/qoffscreen.dll" -Destination "$stageRoot/platforms"
    @('[Paths]', 'Prefix=.', 'Plugins=.') | Set-Content -LiteralPath "$stageRoot/qt.conf" -Encoding ASCII

    $licenseRoot = Join-Path $stageRoot 'licenses'
    $sourceRoot = Join-Path $stageRoot 'third-party-sources'
    New-Item -ItemType Directory -Path $licenseRoot,$sourceRoot -Force | Out-Null
    $sourceCache = Join-Path $PSScriptRoot '.runtime/sources'
    New-Item -ItemType Directory -Path $sourceCache -Force | Out-Null
    $sources = Get-Content -LiteralPath "$PSScriptRoot/packaging/sources.json" -Raw | ConvertFrom-Json
    foreach ($source in $sources) {
        $archive = Join-Path $sourceCache $source.file
        if (!(Test-Path -LiteralPath $archive)) {
            $download = "$archive.download"
            Invoke-WebRequest -UseBasicParsing -Uri $source.url -OutFile $download
            if ((Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash -ne $source.sha256) { throw "Source checksum mismatch: $($source.file)" }
            Move-Item -LiteralPath $download -Destination $archive
        }
        if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $source.sha256) { throw "Source checksum mismatch: $($source.file)" }
        Copy-Item -LiteralPath $archive -Destination $sourceRoot
    }
    Copy-Item -LiteralPath "$PSScriptRoot/packaging/sources.json" -Destination $sourceRoot
    Copy-Item -LiteralPath "$PSScriptRoot/packaging/REBUILD-LIBRARIES.md" -Destination $sourceRoot
    $patchRoot = Join-Path $sourceRoot 'bitmusic-gme'
    New-Item -ItemType Directory -Force -Path "$patchRoot/native","$patchRoot/cmake" | Out-Null
    Copy-Item -LiteralPath "$PSScriptRoot/native/gme-taps" -Destination "$patchRoot/native" -Recurse
    Copy-Item -LiteralPath "$PSScriptRoot/cmake/PatchGme.cmake" -Destination "$patchRoot/cmake"
    Copy-Item -LiteralPath "$PSScriptRoot/packaging/gme-CMakeLists.txt" -Destination "$patchRoot/CMakeLists.txt"
    Copy-Item -LiteralPath "$PSScriptRoot/LICENSES" -Destination $patchRoot -Recurse
    Copy-Item -LiteralPath "$PSScriptRoot/LICENSE" -Destination $patchRoot
    Copy-Item -Path "$PSScriptRoot/LICENSES/*" -Destination $licenseRoot
    Copy-Item -LiteralPath "$buildRoot/_deps/gme-src/license.txt" -Destination "$licenseRoot/libgme-LGPL-2.1.txt"
    Copy-Item -LiteralPath "$buildRoot/_deps/zlib-src/LICENSE" -Destination "$licenseRoot/zlib.txt"
    foreach ($component in @('gcc','mingw-w64','winpthreads')) {
        Copy-Item -LiteralPath "$CompilerRoot/licenses/$component" -Destination $licenseRoot -Recurse
    }
    # Include upstream license texts and human-readable copyright/attribution data.
    foreach ($module in @('qtbase','qtmultimedia','qtdeclarative','qtshadertools')) {
        $moduleLicenses = Join-Path $licenseRoot $module
        New-Item -ItemType Directory -Path $moduleLicenses -Force | Out-Null
        & tar -xf "$sourceCache/$module-everywhere-src-6.11.2.tar.xz" -C $moduleLicenses --strip-components 2 "$module-everywhere-src-6.11.2/LICENSES"
        if ($LASTEXITCODE -ne 0) { throw "Could not extract $module licenses." }
        $sbom = Get-Content -LiteralPath "$QtRoot/sbom/$module-6.11.2.spdx.json" -Raw | ConvertFrom-Json
        $notices = foreach ($item in $sbom.packages) {
            "Component: $($item.name) $($item.versionInfo)"
            "License: $($item.licenseConcluded)"
            $item.copyrightText
            ''
        }
        $notices += foreach ($item in $sbom.hasExtractedLicensingInfos) {
            "License: $($item.licenseId)"
            $item.extractedText
            ''
        }
        $notices | Set-Content -LiteralPath "$licenseRoot/$module-NOTICES.txt" -Encoding UTF8
    }
    Copy-Item -LiteralPath "$PSScriptRoot/packaging/README.txt" -Destination $stageRoot
    Copy-Item -LiteralPath "$PSScriptRoot/LICENSE" -Destination $stageRoot
    foreach ($launcher in @('Start-Diagnostics.cmd','Start-Software.cmd','Start-OpenGL.cmd')) {
        Copy-Item -LiteralPath (Join-Path "$PSScriptRoot/packaging" $launcher) -Destination $stageRoot
    }
    New-Item -ItemType Directory -Path "$stageRoot/themes" -Force | Out-Null
    Copy-Item -LiteralPath "$PSScriptRoot/packaging/THEMES.txt" -Destination "$stageRoot/themes/README.txt"
    Copy-Item -LiteralPath "$PSScriptRoot/THIRD-PARTY.md" -Destination $licenseRoot

    # Fail packaging if a binary imports a DLL that is neither shipped nor a Windows component.
    $dependencies = foreach ($binary in Get-ChildItem -LiteralPath $stageRoot -Recurse -File | Where-Object { $_.Extension -in '.dll','.exe' }) {
        $headers = & "$CompilerRoot/bin/objdump.exe" -p $binary.FullName
        if ($LASTEXITCODE -ne 0) { throw "Cannot inspect $($binary.Name)." }
        foreach ($line in $headers) {
            if ($line -match 'DLL Name: (\S+)') {
                $dll = $Matches[1]
                $bundled = Test-Path -LiteralPath (Join-Path $stageRoot $dll)
                $system = $dll -match '^(api-ms-win-|ext-ms-)' -or (Test-Path -LiteralPath (Join-Path "$env:SystemRoot/System32" $dll))
                if (!$bundled -and !$system) { throw "Missing runtime: $($binary.Name) -> $dll" }
                "$($binary.Name) -> $dll"
            }
        }
    }
    $dependencies | Sort-Object -Unique | Set-Content -LiteralPath "$buildRoot/deployment-dependencies.txt" -Encoding UTF8
    $revision = & git -c "safe.directory=$($PSScriptRoot.Replace('\','/'))" -C $PSScriptRoot rev-parse HEAD
    $changes = & git -c "safe.directory=$($PSScriptRoot.Replace('\','/'))" -C $PSScriptRoot status --porcelain --untracked-files=no
    @("Version: $version", "Platform: Windows x64", "Source revision: $revision", "Tracked working tree changed: $([bool]$changes)",
      "Qt: 6.11.2 (DLLs)", 'libgme: 0.6.5 + Bit Music taps/YM2413, Nuked OPN2 (DLL)', 'zlib: 1.3.2 (static)',
      "Built UTC: $([DateTime]::UtcNow.ToString('u'))") | Set-Content -LiteralPath "$stageRoot/BUILD.txt" -Encoding UTF8
    $manifest = foreach ($file in Get-ChildItem -LiteralPath $stageRoot -Recurse -File | Sort-Object FullName) {
        $relative = $file.FullName.Substring($stageRoot.Length + 1).Replace('\','/')
        "$((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant())  $relative"
    }
    $manifest | Set-Content -LiteralPath "$stageRoot/SHA256SUMS.txt" -Encoding ASCII
    New-Item -ItemType Directory -Path (Split-Path -Parent $OutputPath) -Force | Out-Null
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory($stageParent, $OutputPath, [IO.Compression.CompressionLevel]::Optimal, $false)
    $zipHash = (Get-FileHash -LiteralPath $OutputPath -Algorithm SHA256).Hash.ToLowerInvariant()
    "$zipHash  $([IO.Path]::GetFileName($OutputPath))" | Set-Content -LiteralPath "$OutputPath.sha256" -Encoding ASCII
    Write-Output "Portable ZIP: $OutputPath"
    Write-Output "Size: $([Math]::Round((Get-Item -LiteralPath $OutputPath).Length / 1MB, 1)) MiB"
} finally { $env:PATH = $savedPath }
