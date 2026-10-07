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
foreach ($line in Get-Content -LiteralPath "$playerRoot/SHA256SUMS.txt") {
    if ($line -notmatch '^([a-f0-9]{64})  (.+)$') { throw 'Invalid manifest entry.' }
    $expectedHash = $Matches[1]
    $filePath = [IO.Path]::GetFullPath((Join-Path $playerRoot $Matches[2]))
    if (!$filePath.StartsWith($playerRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Manifest path escapes package.' }
    if ((Get-FileHash -LiteralPath $filePath -Algorithm SHA256).Hash -ne $expectedHash) { throw "Package checksum mismatch: $filePath" }
}
if (Get-ChildItem -LiteralPath $playerRoot -Recurse -File | Where-Object { $_.Extension -in '.nsf','.nsfe','.vgm','.vgz','.spc','.sid','.gbs','.ay','.mdx','.obj','.pdb' }) {
    throw 'The runtime package includes music or development output.'
}
if (Test-Path -LiteralPath (Join-Path $playerRoot 'BitMusicVisualizer.ini')) {
    throw 'The runtime package includes personal settings.'
}
$fixtureRoot = Join-Path $checkRoot 'test fixtures'
New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null
foreach ($fixture in @('demo.nsf','demo.vgm','demo.vgz','demo.spc','named.nsfe','intro-loop.vgm')) {
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
    if (!$process.WaitForExit(60000)) { throw 'Portable tap checks timed out.' }
    if ($process.ExitCode -ne 0) { throw "Portable tap checks failed: $fixtureRoot/tap-checks.txt (exit $($process.ExitCode))" }
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
