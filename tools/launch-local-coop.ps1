param(
    [switch]$RestartOnDeath
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$releaseDir = Join-Path $repoRoot 'Windows\Rott94\Rott94.Windows\out\Win32\Release'
$steamAssets = 'C:\Program Files (x86)\Steam\steamapps\common\Rise of the Triad - Ludicrous Edition\assets'
$fallbackAssets = Join-Path $repoRoot 'Windows\out\x64\Release'
$assetSourceDir = if (Test-Path -LiteralPath (Join-Path $steamAssets 'darkwar.wad')) {
    $steamAssets
} else {
    $fallbackAssets
}
$testRoot = Join-Path $releaseDir 'local-coop-test'
$hostDir = Join-Path $testRoot 'host'
$clientDir = Join-Path $testRoot 'client'
$gameExeName = 'Rott94.Windows.Release.exe'
$gameExe = Join-Path $releaseDir $gameExeName

$runtimeFiles = @(
    $gameExeName,
    'SDL2.dll',
    'SDL2_mixer.dll',
    'libmodplug-1.dll',
    'libogg-0.dll',
    'libopus-0.dll',
    'libopusfile-0.dll'
)

$assetFiles = @(
    'darkwar.wad',
    'darkwar.rtl',
    'remote1.rts'
)

if (-not (Test-Path -LiteralPath $gameExe -PathType Leaf)) {
    throw "The co-op build was not found at: $gameExe"
}

foreach ($directory in @($hostDir, $clientDir)) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    $assetDirectory = Join-Path $directory 'assets'
    New-Item -ItemType Directory -Path $assetDirectory -Force | Out-Null

    foreach ($fileName in $runtimeFiles) {
        $source = Join-Path $releaseDir $fileName
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "A required game file was not found: $source"
        }

        Copy-Item -LiteralPath $source -Destination (Join-Path $directory $fileName) -Force
    }

    foreach ($fileName in $assetFiles) {
        $source = Join-Path $assetSourceDir $fileName
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "A required game asset was not found: $source"
        }

        Copy-Item -LiteralPath $source -Destination (Join-Path $assetDirectory $fileName) -Force
    }

    $gusMidi = Join-Path $assetSourceDir 'gusmidi.ini'
    if (Test-Path -LiteralPath $gusMidi -PathType Leaf) {
        Copy-Item -LiteralPath $gusMidi -Destination (Join-Path $directory 'gusmidi.ini') -Force
    }
}

$hostExe = Join-Path $hostDir $gameExeName
$clientExe = Join-Path $clientDir $gameExeName
$safeLocalArguments = @('-window', '-nowait', '-nosound', '-nojoys', '-nomouse')
$coopDeathArguments = if ($RestartOnDeath) { @('-cooprestart') } else { @() }
$hostOutputLog = Join-Path $hostDir 'coop-host.out.log'
$hostErrorLog = Join-Path $hostDir 'coop-host.err.log'
$clientOutputLog = Join-Path $clientDir 'coop-client.out.log'
$clientErrorLog = Join-Path $clientDir 'coop-client.err.log'

Write-Host 'Starting the co-op host...'
$hostProcess = Start-Process -FilePath $hostExe `
    -ArgumentList (@('-net', '-server', '-players', '2') + $safeLocalArguments + $coopDeathArguments) `
    -WorkingDirectory $hostDir `
    -RedirectStandardOutput $hostOutputLog `
    -RedirectStandardError $hostErrorLog `
    -PassThru

Start-Sleep -Milliseconds 800

Write-Host 'Starting the local client...'
$clientProcess = Start-Process -FilePath $clientExe `
    -ArgumentList (@('-net', '127.0.0.1') + $safeLocalArguments + $coopDeathArguments) `
    -WorkingDirectory $clientDir `
    -RedirectStandardOutput $clientOutputLog `
    -RedirectStandardError $clientErrorLog `
    -PassThru

Write-Host "Host PID:   $($hostProcess.Id)"
Write-Host "Client PID: $($clientProcess.Id)"
Write-Host 'Both copies should open after their localhost handshake completes.'
Write-Host 'Use Alt+Tab to switch between them. The host chooses CO-OP CAMPAIGN.'
if ($RestartOnDeath) {
    Write-Host 'Death rule: restart the level for both players.'
} else {
    Write-Host 'Death rule: respawn the dead player at the level start with reset gear.'
}
Write-Host "Host log:   $hostOutputLog"
Write-Host "Client log: $clientOutputLog"
