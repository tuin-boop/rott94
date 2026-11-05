param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',
    [int]$CaptureTimeoutSeconds = 15,
    [switch]$SkipDemos,
    [switch]$SkipMontage
)

$ErrorActionPreference = 'Stop'
$workspace = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $workspace "Windows\Rott94\Rott94.Windows\out\Win32\$Configuration"
$exe = Join-Path $buildDir "Rott94.Windows.$Configuration.exe"
$runtimeDir = $buildDir

if (-not (Test-Path -LiteralPath $exe)) {
    throw "Renderer test executable was not found: $exe"
}
if (-not (Test-Path -LiteralPath (Join-Path $runtimeDir 'assets\darkwar.wad'))) {
    # Local development normally registers/copies the commercial data beside
    # the Release executable only.  A Debug binary can safely run with that
    # directory as its working directory while still exercising Debug code.
    $releaseRuntimeDir = Join-Path $workspace 'Windows\Rott94\Rott94.Windows\out\Win32\Release'
    if (Test-Path -LiteralPath (Join-Path $releaseRuntimeDir 'assets\darkwar.wad')) {
        $runtimeDir = $releaseRuntimeDir
    } else {
        throw "Renderer test assets were not found beneath: $buildDir\assets"
    }
}

# These positions cover the Area 1 entrance, all four headings, true vertical
# look, indoor/outdoor transitions, doors, tall walls, and the late corridor.
$probes = @(
    @(4, 67, 0, 0),
    @(4, 67, 90, 0),
    @(4, 67, 180, 0),
    @(4, 67, 270, 0),
    @(11, 67, 0, 55),
    @(11, 67, 0, -55),
    @(18, 67, 0, 0),
    @(18, 72, 0, 0),
    @(27, 71, 0, 0),
    @(34, 72, 0, 0),
    @(42, 82, 0, 0),
    @(50, 72, 180, 0),
    @(68, 74, 0, 0)
)

function Stop-TestProcess([Diagnostics.Process]$Process) {
    if ($Process -and -not $Process.HasExited) {
        Stop-Process -Id $Process.Id -Force
        $Process.WaitForExit()
    }
}

function Invoke-RendererCapture(
    [string]$Renderer,
    [object[]]$Probe,
    [string[]]$ExtraArguments = @(),
    [string]$OutputTag = ''
) {
    $x, $y, $yaw, $pitch = $Probe
    $prefix = if ($Renderer -eq 'advanced') { 'advanced' } else { 'classic' }
    $capture = Join-Path $runtimeDir "$prefix-probe-$x-$y-$yaw-$pitch.bmp"
    $before = if (Test-Path -LiteralPath $capture) {
        (Get-Item -LiteralPath $capture).LastWriteTimeUtc
    } else {
        [datetime]::MinValue
    }
    $arguments = @('-nowait', '-nosound', '-now', '-window')
    if ($Renderer -eq 'advanced') {
        $arguments += @('-advancedrenderer', '-advancedcapture')
    } else {
        $arguments += '-classiccapture'
    }
    $arguments += @('-advancedprobecamera', "$x", "$y", "$yaw", "$pitch")
    $arguments += $ExtraArguments
    $process = Start-Process -FilePath $exe -WorkingDirectory $runtimeDir `
        -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $deadline = (Get-Date).AddSeconds($CaptureTimeoutSeconds)
    $updated = $false
    try {
        do {
            Start-Sleep -Milliseconds 150
            if (Test-Path -LiteralPath $capture) {
                $updated = (Get-Item -LiteralPath $capture).LastWriteTimeUtc -gt $before
            }
        } while (-not $updated -and -not $process.HasExited -and (Get-Date) -lt $deadline)
        if (-not $updated) {
            throw "$Renderer capture timed out at camera $x,$y yaw $yaw pitch $pitch"
        }
        # SDL_SaveBMP updates the directory entry before all buffered bytes have
        # necessarily reached disk.  Let the capture close before terminating.
        Start-Sleep -Milliseconds 700
    } finally {
        Stop-TestProcess $process
    }
    if ($OutputTag) {
        $taggedCapture = Join-Path $runtimeDir "$prefix-$OutputTag.bmp"
        Copy-Item -LiteralPath $capture -Destination $taggedCapture -Force
        return $taggedCapture
    }
    return $capture
}

function Invoke-DemoTest([int]$DemoNumber) {
    $arguments = @('-nowait', '-nosound', '-window', '-advancedrenderer',
        '-playdemo', "$DemoNumber", '-demoexit')
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process -FilePath $exe -WorkingDirectory $runtimeDir `
        -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(60000)) {
        Stop-TestProcess $process
        throw "Advanced renderer demo $DemoNumber exceeded 60 seconds"
    }
    $timer.Stop()
    # QuitGame uses exit(1) after a successful command-line demo.  Treat any
    # other code as a regression until that legacy convention is changed.
    if ($process.ExitCode -ne 1) {
        throw "Advanced renderer demo $DemoNumber exited with $($process.ExitCode)"
    }
    [pscustomobject]@{
        Demo = $DemoNumber
        ExitCode = $process.ExitCode
        Seconds = [math]::Round($timer.Elapsed.TotalSeconds, 3)
    }
}

$started = Get-Date
$captures = New-Object System.Collections.Generic.List[string]
foreach ($probe in $probes) {
    $captures.Add((Invoke-RendererCapture 'classic' $probe))
    $captures.Add((Invoke-RendererCapture 'advanced' $probe))
}

# Exercise a real Area 1 door in its closed, half-open, and fully-open states.
# The engine-side probe drives DoorOpening so area connectivity and visibility
# are identical to live gameplay, rather than changing only the artwork frame.
$doorStates = @(0, 32768, 65535)
foreach ($doorState in $doorStates) {
    $doorArguments = @('-advanceddoorprobe', '23', '72', "$doorState")
    $captures.Add((Invoke-RendererCapture 'classic' @(22, 72, 0, 0) `
        $doorArguments "door-23-72-$doorState"))
    $captures.Add((Invoke-RendererCapture 'advanced' @(22, 72, 0, 0) `
        $doorArguments "door-23-72-$doorState"))
}

# The flashlight is an advanced-renderer feature, so verify that enabling it
# materially changes the same deterministic entrance view.
$unlitEntrance = Join-Path $runtimeDir 'advanced-probe-4-67-0-0.bmp'
$unlitEntranceHash = (Get-FileHash -LiteralPath $unlitEntrance -Algorithm SHA256).Hash
$flashlightCapture = Invoke-RendererCapture 'advanced' @(4, 67, 0, 0) `
    @('-flashlight') 'flashlight-on'
$captures.Add($flashlightCapture)
if ($unlitEntranceHash -eq (Get-FileHash -LiteralPath $flashlightCapture `
    -Algorithm SHA256).Hash) {
    throw 'The advanced flashlight capture was identical to the unlit view'
}

$demoResults = @()
if (-not $SkipDemos) {
    $demoResults += Invoke-DemoTest 1
    $demoResults += Invoke-DemoTest 2
}

$montage = $null
if (-not $SkipMontage) {
    $magick = Get-Command magick -ErrorAction SilentlyContinue
    if ($magick) {
        $rowFiles = New-Object System.Collections.Generic.List[string]
        foreach ($probe in $probes) {
            $x, $y, $yaw, $pitch = $probe
            $classic = Join-Path $runtimeDir "classic-probe-$x-$y-$yaw-$pitch.bmp"
            $advanced = Join-Path $runtimeDir "advanced-probe-$x-$y-$yaw-$pitch.bmp"
            $row = Join-Path $runtimeDir "renderer-test-row-$x-$y-$yaw-$pitch.png"
            & $magick.Source $classic -resize '480x270!' $advanced -resize '480x270!' +append $row
            if ($LASTEXITCODE -ne 0) { throw "ImageMagick failed while making $row" }
            $rowFiles.Add($row)
        }
        foreach ($doorState in $doorStates) {
            $classic = Join-Path $runtimeDir "classic-door-23-72-$doorState.bmp"
            $advanced = Join-Path $runtimeDir "advanced-door-23-72-$doorState.bmp"
            $row = Join-Path $runtimeDir "renderer-test-door-$doorState.png"
            & $magick.Source $classic -resize '480x270!' $advanced -resize '480x270!' +append $row
            if ($LASTEXITCODE -ne 0) { throw "ImageMagick failed while making $row" }
            $rowFiles.Add($row)
        }
        $montage = Join-Path $runtimeDir 'renderer-regression-montage.png'
        & $magick.Source @rowFiles -append $montage
        if ($LASTEXITCODE -ne 0) { throw 'ImageMagick failed while making the renderer montage' }
    }
}

$result = [pscustomobject]@{
    Configuration = $Configuration
    RuntimeDirectory = $runtimeDir
    ProbePairs = $probes.Count
    DoorStatePairs = $doorStates.Count
    FlashlightCaptures = 1
    Captures = $captures.Count
    DemoResults = $demoResults
    Montage = $montage
    Seconds = [math]::Round(((Get-Date) - $started).TotalSeconds, 3)
}
$result | ConvertTo-Json -Depth 4
