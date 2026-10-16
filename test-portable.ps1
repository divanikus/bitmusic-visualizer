param(
    [Parameter(Mandatory=$true)][string]$Archive,
    [string[]]$MusicPaths = @()
)
$ErrorActionPreference = 'Stop'
$Archive = (Resolve-Path -LiteralPath $Archive).Path
$checkRoot = Join-Path $PSScriptRoot ('test-output/p-' + [Guid]::NewGuid().ToString('N').Substring(0, 6))
$unpackRoot = Join-Path $checkRoot ('Run ' + [char]0x0416)
New-Item -ItemType Directory -Path $unpackRoot -Force | Out-Null
Expand-Archive -LiteralPath $Archive -DestinationPath $unpackRoot
$playerRoot = (Get-ChildItem -LiteralPath $unpackRoot -Directory | Select-Object -First 1).FullName
$executable = Join-Path $playerRoot 'bitmusic_visualizer.exe'
if (!(Test-Path -LiteralPath $executable)) { throw 'Archive has no player executable.' }
foreach ($required in @('COPYING','licenses/GPL-3.0.txt','licenses/libsidplayfp-NOTICES.txt',
    'third-party-sources/libsidplayfp-2.16.1.tar.gz','third-party-sources/bitmusic-visualizer-source.tar.xz',
    'licenses/ymfm-BSD-3-Clause.txt','licenses/ymfm-voices-BSD-3-Clause.txt',
    'third-party-sources/ymfm-81aec25ccbb98f4873a255f7551ac4dadac59b4a.tar.gz',
    'roms/ymfm/README.txt','roms/mt32/README.txt','soundfonts/README.txt',
    'licenses/FluidSynth-LGPL-2.1.txt','licenses/Munt-LGPL-2.1.txt',
    'third-party-sources/fluidsynth-2.5.4.tar.gz','third-party-sources/munt-libmt32emu-2.8.3.tar.gz')) {
    if (!(Test-Path -LiteralPath (Join-Path $playerRoot $required))) { throw "Missing corresponding source/license: $required" }
}
foreach ($line in Get-Content -LiteralPath "$playerRoot/SHA256SUMS.txt") {
    if ($line -notmatch '^([a-f0-9]{64})  (.+)$') { throw 'Invalid manifest entry.' }
    $expectedHash = $Matches[1]
    $filePath = [IO.Path]::GetFullPath((Join-Path $playerRoot $Matches[2]))
    if (!$filePath.StartsWith($playerRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Manifest path escapes package.' }
    if ((Get-FileHash -LiteralPath $filePath -Algorithm SHA256).Hash -ne $expectedHash) { throw "Package checksum mismatch: $filePath" }
}
if (Get-ChildItem -LiteralPath $playerRoot -Recurse -File | Where-Object { $_.Extension -in '.m3u','.m3u8','.nsf','.nsfe','.vgm','.vgz','.spc','.sid','.gbs','.ay','.mdx','.obj','.pdb','.rom','.sf2','.sf3','.mid','.midi','.syx','.mod','.m15','.xm','.s3m','.it','.med','.mmd0','.mmd1','.mmd2','.mmd3','.umx','.dbm','.digi','.emod' }) {
    throw 'The runtime package includes music or development output.'
}
if (Test-Path -LiteralPath (Join-Path $playerRoot 'BitMusicVisualizer.ini')) {
    throw 'The runtime package includes personal settings.'
}
$fixtureRoot = Join-Path $checkRoot 'test fixtures'
New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null
foreach ($fixture in @('demo.nsf','demo.vgm','demo.vgz','demo.spc','named.nsfe','intro-loop.vgm',
    'demo.mid','fixture-gm.sf2','long-seek.mid','fractional-end.mid',
    'demo.mod','soundtracker.mod','multichannel.mod','sequences.mod')) {
    Copy-Item -LiteralPath (Join-Path "$PSScriptRoot/test-output" $fixture) -Destination $fixtureRoot
}
$savedEnvironment = @{}
$environmentNames = @('PATH','QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','QT_QPA_PLATFORM','QML2_IMPORT_PATH','QML_IMPORT_PATH','QT_MEDIA_BACKEND','QSG_RHI_BACKEND','QT_QUICK_BACKEND','BITMUSIC_SOFTWARE_SCOPES')
foreach ($name in $environmentNames) { $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
$process = $null
try {
    # Keep Windows itself available, but remove the SDK/compiler/Python from DLL lookup.
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    foreach ($name in $environmentNames | Where-Object { $_ -ne 'PATH' }) { [Environment]::SetEnvironmentVariable($name, $null, 'Process') }
    $testArguments = @('--self-test', ('"' + $fixtureRoot + '"'))
    foreach ($music in $MusicPaths) { $testArguments += ('"' + (Resolve-Path -LiteralPath $music).Path + '"') }
    $process = Start-Process -FilePath $executable -WorkingDirectory $playerRoot -ArgumentList $testArguments `
        -PassThru -WindowStyle Hidden -RedirectStandardOutput "$checkRoot/stdout.txt" -RedirectStandardError "$checkRoot/stderr.txt"
    # Retain the process handle so Windows PowerShell can read ExitCode after exit.
    $null = $process.Handle
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $observed = @{}
    while (!$process.HasExited) {
        foreach ($module in $process.Modules) {
            if ($module.ModuleName -match '^(Qt6|libgme|libgcc|libstdc|libwinpthread|qwindows)') {
                if (!$module.FileName.StartsWith($playerRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
                    throw "Runtime loaded outside portable folder: $($module.FileName)"
                }
                $observed[$module.ModuleName] = $module.FileName
            }
        }
        if ($timer.Elapsed.TotalSeconds -gt 300) { throw 'Portable checks timed out.' }
        Start-Sleep -Milliseconds 500
        $process.Refresh()
    }
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) { throw "Portable self-test failed: $fixtureRoot/native-checks.txt (exit $($process.ExitCode))" }
    $process = Start-Process -FilePath $executable -WorkingDirectory $playerRoot -ArgumentList @('--tap-checks', ('"' + $fixtureRoot + '"')) `
        -PassThru -WindowStyle Hidden -RedirectStandardOutput "$checkRoot/tap-stdout.txt" -RedirectStandardError "$checkRoot/tap-stderr.txt"
    $null = $process.Handle
    # SID and the full Yamaha matrix make this substantially longer than the
    # original GME-only checks, especially on slower machines.
    if (!$process.WaitForExit(300000)) { throw 'Portable tap checks timed out.' }
    if ($process.ExitCode -ne 0) { throw "Portable tap checks failed: $fixtureRoot/tap-checks.txt (exit $($process.ExitCode))" }
    $process = Start-Process -FilePath $executable -WorkingDirectory $playerRoot -ArgumentList @('--midi-checks', ('"' + $fixtureRoot + '"')) `
        -PassThru -WindowStyle Hidden -RedirectStandardOutput "$fixtureRoot/midi-checks.txt" -RedirectStandardError "$checkRoot/midi-stderr.txt"
    $null = $process.Handle
    if (!$process.WaitForExit(60000)) { throw 'Portable MIDI checks timed out.' }
    if ($process.ExitCode -ne 0) { throw "Portable MIDI checks failed (exit $($process.ExitCode))" }
    $process = Start-Process -FilePath $executable -WorkingDirectory $playerRoot -ArgumentList @('--mt32-checks', ('"' + $fixtureRoot + '"')) `
        -PassThru -WindowStyle Hidden -RedirectStandardOutput "$fixtureRoot/mt32-checks.txt" -RedirectStandardError "$checkRoot/mt32-stderr.txt"
    $null = $process.Handle
    if (!$process.WaitForExit(60000)) { throw 'Portable MT-32 resource checks timed out.' }
    if ($process.ExitCode -ne 0) { throw "Portable MT-32 resource checks failed (exit $($process.ExitCode))" }
    $process = Start-Process -FilePath $executable -WorkingDirectory $playerRoot -ArgumentList @('--tracker-checks', ('"' + $fixtureRoot + '"')) `
        -PassThru -WindowStyle Hidden -RedirectStandardOutput "$fixtureRoot/tracker-checks.txt" -RedirectStandardError "$checkRoot/tracker-stderr.txt"
    $null = $process.Handle
    if (!$process.WaitForExit(60000)) { throw 'Portable tracker checks timed out.' }
    if ($process.ExitCode -ne 0) { throw "Portable tracker checks failed (exit $($process.ExitCode))" }
    $process = Start-Process -FilePath $executable -WorkingDirectory $playerRoot -ArgumentList @('--ttt-checks', ('"' + $fixtureRoot + '"')) `
        -PassThru -WindowStyle Hidden -RedirectStandardOutput "$fixtureRoot/tia-checks.txt" -RedirectStandardError "$checkRoot/tia-stderr.txt"
    $null = $process.Handle
    if (!$process.WaitForExit(60000)) { throw 'Portable TIA checks timed out.' }
    if ($process.ExitCode -ne 0) { throw "Portable TIA checks failed (exit $($process.ExitCode))" }
    $process = Start-Process -FilePath $executable -WorkingDirectory $playerRoot -ArgumentList @('--playlist-checks', ('"' + $fixtureRoot + '"')) `
        -PassThru -WindowStyle Hidden -RedirectStandardOutput "$fixtureRoot/playlist-checks.txt" -RedirectStandardError "$checkRoot/playlist-stderr.txt"
    $null = $process.Handle
    if (!$process.WaitForExit(60000)) { throw 'Portable playlist checks timed out.' }
    if ($process.ExitCode -ne 0) { throw "Portable playlist checks failed (exit $($process.ExitCode))" }
    $process = Start-Process -FilePath $executable -WorkingDirectory $playerRoot -ArgumentList @('--music-view-checks', ('"' + $fixtureRoot + '"')) `
        -PassThru -WindowStyle Hidden -RedirectStandardOutput "$fixtureRoot/music-view-checks.txt" -RedirectStandardError "$checkRoot/music-view-stderr.txt"
    $null = $process.Handle
    if (!$process.WaitForExit(60000)) { throw 'Portable music view checks timed out.' }
    if ($process.ExitCode -ne 0) { throw "Portable music view checks failed (exit $($process.ExitCode))" }
    $process = Start-Process -FilePath $executable -WorkingDirectory $playerRoot -ArgumentList @('--gpu-checks', ('"' + $fixtureRoot + '"')) `
        -PassThru -WindowStyle Hidden -RedirectStandardOutput "$checkRoot/gpu-stdout.txt" -RedirectStandardError "$checkRoot/gpu-stderr.txt"
    $null = $process.Handle
    if (!$process.WaitForExit(60000)) { throw 'Portable GPU checks timed out.' }
    if ($process.ExitCode -ne 0) { throw "Portable GPU checks failed: $fixtureRoot/gpu-checks.txt (exit $($process.ExitCode))" }
    foreach ($required in @('Qt6Core.dll','Qt6Widgets.dll','Qt6Multimedia.dll','Qt6Quick.dll','Qt6QuickWidgets.dll','Qt6Qml.dll','libgme.dll','qwindows.dll')) {
        if (!$observed.ContainsKey($required)) { throw "Did not observe runtime loading: $required" }
    }
    $observed.GetEnumerator() | Sort-Object Name | ForEach-Object { "$($_.Name): $($_.Value)" } | Set-Content -LiteralPath "$checkRoot/loaded-runtime.txt" -Encoding UTF8
    Get-Content -LiteralPath "$fixtureRoot/native-checks.txt"
    Write-Output "Portable archive hashes, content check, relocated runtime loading and native tests PASS. Report: $checkRoot"
} finally {
    if ($process -and !$process.HasExited) { $process.Kill(); $process.WaitForExit() }
    foreach ($name in $environmentNames) { [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process') }
}
