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
    @(11, 67, 0, 89),
    @(11, 67, 0, -89),
    @(18, 67, 0, 0),
    @(18, 72, 0, 0),
    # Floor plate, pickup, spring, and actor scale/placement coverage.
    @(20, 74, 0, 45),
    @(10, 67, 0, 10),
    @(31, 72, 0, 0),
    @(16, 72, 0, 0),
    # Fire-jet corridor from an adjacent diagonal tile.
    @(79, 75, 45, 20),
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

function Assert-CaptureBitmap([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 54 -or $bytes[0] -ne 0x42 -or $bytes[1] -ne 0x4d) {
        throw "Renderer capture is not a valid BMP: $Path"
    }
    $width = [BitConverter]::ToInt32($bytes, 18)
    $height = [math]::Abs([BitConverter]::ToInt32($bytes, 22))
    if ($width -lt 320 -or $height -lt 200) {
        throw "Renderer capture has an invalid size ${width}x${height}: $Path"
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
        Assert-CaptureBitmap $taggedCapture
        return $taggedCapture
    }
    Assert-CaptureBitmap $capture
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

# Exercise real Area 1 doors in their closed, half-open, and fully-open states.
# The engine-side probe drives DoorOpening so area connectivity and visibility
# are identical to live gameplay, rather than changing only the artwork frame.
$doorStates = @(0, 32768, 65535)
$doorProbes = @(
    # Vertical door plane at the Area 1 entrance.
    [pscustomobject]@{ X = 23; Y = 72; Camera = @(22, 72, 0, 0) },
    # Horizontal three-door bank with different artwork deeper in Area 1.
    [pscustomobject]@{ X = 42; Y = 28; Camera = @(42, 30, 90, 0) }
)
foreach ($doorProbe in $doorProbes) {
    $doorHashes = @{ classic = @(); advanced = @() }
    foreach ($doorState in $doorStates) {
        $doorTag = "door-$($doorProbe.X)-$($doorProbe.Y)-$doorState"
        $doorArguments = @('-advanceddoorprobe', "$($doorProbe.X)",
            "$($doorProbe.Y)", "$doorState")
        $classicDoorCapture = Invoke-RendererCapture 'classic' `
            $doorProbe.Camera $doorArguments $doorTag
        $advancedDoorCapture = Invoke-RendererCapture 'advanced' `
            $doorProbe.Camera $doorArguments $doorTag
        $captures.Add($classicDoorCapture)
        $captures.Add($advancedDoorCapture)
        $doorHashes.classic += (Get-FileHash -LiteralPath $classicDoorCapture `
            -Algorithm SHA256).Hash
        $doorHashes.advanced += (Get-FileHash -LiteralPath $advancedDoorCapture `
            -Algorithm SHA256).Hash
    }
    foreach ($renderer in 'classic', 'advanced') {
        if (($doorHashes[$renderer] | Select-Object -Unique).Count -ne `
            $doorStates.Count) {
            throw "$renderer door states did not all render differently at $($doorProbe.X),$($doorProbe.Y)"
        }
    }
}

# Elevator disks use five perspective frames and travel through the full level
# height.  Pin one real Area 1 disk at the ceiling, midpoint, and floor so a
# future coordinate/sign regression cannot bury it or make it jump in size.
$diskStates = @(0, 64, 128)
$diskHashes = @{ classic = @(); advanced = @() }
foreach ($diskState in $diskStates) {
    $diskTag = "disk-38-70-$diskState"
    $diskArguments = @('-advanceddiskprobe', '38', '70', "$diskState")
    $classicDiskCapture = Invoke-RendererCapture 'classic' @(38, 68, 270, 5) `
        $diskArguments $diskTag
    $advancedDiskCapture = Invoke-RendererCapture 'advanced' @(38, 68, 270, 5) `
        $diskArguments $diskTag
    $captures.Add($classicDiskCapture)
    $captures.Add($advancedDiskCapture)
    $diskHashes.classic += (Get-FileHash -LiteralPath $classicDiskCapture `
        -Algorithm SHA256).Hash
    $diskHashes.advanced += (Get-FileHash -LiteralPath $advancedDiskCapture `
        -Algorithm SHA256).Hash
}
foreach ($renderer in 'classic', 'advanced') {
    if (($diskHashes[$renderer] | Select-Object -Unique).Count -ne `
        $diskStates.Count) {
        throw "$renderer elevator disk states did not all render differently"
    }
}

# Three adjacent static lifts exercise the classic near-plane edge case: the
# closest is behind the camera, the next is entirely above the viewport, and
# the farthest is only partly visible at the ceiling.  This caught a one-tile
# vertical-baseline mismatch that the moving lift alone could not isolate.
$staticDiskXs = @(16, 17, 18)
foreach ($staticDiskX in $staticDiskXs) {
    $diskTag = "static-disk-$staticDiskX-71-64"
    $diskArguments = @('-advanceddiskprobe', "$staticDiskX", '71', '64')
    $classicDiskCapture = Invoke-RendererCapture 'classic' @(16, 72, 0, 0) `
        $diskArguments $diskTag
    $advancedDiskCapture = Invoke-RendererCapture 'advanced' @(16, 72, 0, 0) `
        $diskArguments $diskTag
    $captures.Add($classicDiskCapture)
    $captures.Add($advancedDiskCapture)
}

# The flashlight is an advanced-renderer feature, so verify that enabling it
# materially changes the same deterministic view in Area 1's darker door room.
$flashlightProbe = @(18, 73, 90, 0)
$unlitFlashlightCapture = Invoke-RendererCapture 'advanced' $flashlightProbe `
    @() 'flashlight-off'
$captures.Add($unlitFlashlightCapture)
$unlitFlashlightHash = (Get-FileHash -LiteralPath $unlitFlashlightCapture `
    -Algorithm SHA256).Hash
$flashlightCapture = Invoke-RendererCapture 'advanced' $flashlightProbe `
    @('-flashlight') 'flashlight-on'
$captures.Add($flashlightCapture)
if ($unlitFlashlightHash -eq (Get-FileHash -LiteralPath $flashlightCapture `
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
        foreach ($doorProbe in $doorProbes) {
            foreach ($doorState in $doorStates) {
                $doorTag = "door-$($doorProbe.X)-$($doorProbe.Y)-$doorState"
                $classic = Join-Path $runtimeDir "classic-$doorTag.bmp"
                $advanced = Join-Path $runtimeDir "advanced-$doorTag.bmp"
                $row = Join-Path $runtimeDir "renderer-test-$doorTag.png"
                & $magick.Source $classic -resize '480x270!' $advanced -resize '480x270!' +append $row
                if ($LASTEXITCODE -ne 0) { throw "ImageMagick failed while making $row" }
                $rowFiles.Add($row)
            }
        }
        foreach ($diskState in $diskStates) {
            $diskTag = "disk-38-70-$diskState"
            $classic = Join-Path $runtimeDir "classic-$diskTag.bmp"
            $advanced = Join-Path $runtimeDir "advanced-$diskTag.bmp"
            $row = Join-Path $runtimeDir "renderer-test-$diskTag.png"
            & $magick.Source $classic -resize '480x270!' $advanced -resize '480x270!' +append $row
            if ($LASTEXITCODE -ne 0) { throw "ImageMagick failed while making $row" }
            $rowFiles.Add($row)
        }
        foreach ($staticDiskX in $staticDiskXs) {
            $diskTag = "static-disk-$staticDiskX-71-64"
            $classic = Join-Path $runtimeDir "classic-$diskTag.bmp"
            $advanced = Join-Path $runtimeDir "advanced-$diskTag.bmp"
            $row = Join-Path $runtimeDir "renderer-test-$diskTag.png"
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
    DoorStatePairs = $doorStates.Count * $doorProbes.Count
    DiskStatePairs = $diskStates.Count
    StaticDiskPairs = $staticDiskXs.Count
    FlashlightCaptures = 2
    Captures = $captures.Count
    DemoResults = $demoResults
    Montage = $montage
    Seconds = [math]::Round(((Get-Date) - $started).TotalSeconds, 3)
}
$result | ConvertTo-Json -Depth 4
