param(
    [Parameter(Mandatory = $true)][string]$InputFont,
    [Parameter(Mandatory = $true)][string]$Wad,
    [Parameter(Mandatory = $true)][string]$OutputFont,
    [ValidateRange(1, 4)][int]$Scale = 2
)

$ErrorActionPreference = 'Stop'

$fon = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $InputFont))
if ([Text.Encoding]::ASCII.GetString($fon, 0, 4) -ne 'FON2') {
    throw 'Input is not a FON2 font.'
}

function U16([byte[]]$bytes, [int]$offset) {
    return [BitConverter]::ToUInt16($bytes, $offset)
}
function I32([byte[]]$bytes, [int]$offset) {
    return [BitConverter]::ToInt32($bytes, $offset)
}

$height = U16 $fon 4
$first = [int]$fon[6]
$last = [int]$fon[7]
$mono = $fon[8] -ne 0
$paletteCount = [int]$fon[10] + 1
$flags = [int]$fon[11]
$cursor = 12
if (($flags -band 1) -ne 0) { $cursor += 2 }

$count = $last - $first + 1
$widths = New-Object 'int[]' $count
if ($mono) {
    $width = U16 $fon $cursor
    $cursor += 2
    for ($i = 0; $i -lt $count; $i++) { $widths[$i] = $width }
} else {
    for ($i = 0; $i -lt $count; $i++) {
        $widths[$i] = U16 $fon $cursor
        $cursor += 2
    }
}

$fontPalette = @()
for ($i = 0; $i -lt $paletteCount; $i++) {
    $fontPalette += ,@([int]$fon[$cursor], [int]$fon[$cursor + 1], [int]$fon[$cursor + 2])
    $cursor += 3
}

$wadBytes = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Wad))
$lumpCount = I32 $wadBytes 4
$directory = I32 $wadBytes 8
$palOffset = -1
$palSize = 0
for ($i = 0; $i -lt $lumpCount; $i++) {
    $entry = $directory + $i * 16
    $name = [Text.Encoding]::ASCII.GetString($wadBytes, $entry + 8, 8).Trim([char]0)
    if ($name -ieq 'PAL') {
        $palOffset = I32 $wadBytes $entry
        $palSize = I32 $wadBytes ($entry + 4)
        break
    }
}
if ($palOffset -lt 0 -or $palSize -lt 768) { throw 'PAL lump not found in WAD.' }

$rottPalette = @()
for ($i = 0; $i -lt 256; $i++) {
    # ROTT stores VGA 0..63 values.
    $r = ([int]$wadBytes[$palOffset + $i * 3]) * 4
    $g = ([int]$wadBytes[$palOffset + $i * 3 + 1]) * 4
    $b = ([int]$wadBytes[$palOffset + $i * 3 + 2]) * 4
    $rottPalette += ,@($r, $g, $b)
}

$paletteMap = New-Object 'byte[]' $paletteCount
$paletteMap[0] = 0
for ($i = 1; $i -lt $paletteCount; $i++) {
    # TinyBaby is a crisp bitmap face. Use ROTT's light cream entry so it
    # remains readable on the new flat menu rather than inheriting FON2's
    # nearly-black source palette.
    $paletteMap[$i] = [byte]235
}

$characters = @{}
for ($character = $first; $character -le $last; $character++) {
    $width = $widths[$character - $first]
    $size = $width * $height
    $pixels = New-Object 'byte[]' $size
    $position = 0
    while ($position -lt $size) {
        $code = [int]$fon[$cursor++]
        if ($code -ge 128) { $code -= 256 }
        if ($code -ge 0) {
            $run = [int]$code + 1
            for ($j = 0; $j -lt $run; $j++) { $pixels[$position++] = $fon[$cursor++] }
        } elseif ($code -ne -128) {
            $value = $fon[$cursor++]
            $run = -[int]$code + 1
            for ($j = 0; $j -lt $run; $j++) { $pixels[$position++] = $value }
        }
    }
    $characters[$character] = $pixels
}

# ROTT font_t: height, 256 widths, 256 absolute offsets, column-major pixels.
$headerSize = 770
$data = [Collections.Generic.List[byte]]::new()
$header = New-Object 'byte[]' $headerSize
[BitConverter]::GetBytes([int16]($height * $Scale)).CopyTo($header, 0)
for ($character = 32; $character -le 126; $character++) {
    $slot = $character - 31
    if (-not $characters.ContainsKey($character)) { continue }
    $sourceWidth = $widths[$character - $first]
    $targetWidth = $sourceWidth * $Scale + 1
    $header[2 + $slot] = [byte]$targetWidth
    $offset = $headerSize + $data.Count
    [BitConverter]::GetBytes([int16]$offset).CopyTo($header, 258 + $slot * 2)
    $source = $characters[$character]
    for ($x = 0; $x -lt $targetWidth; $x++) {
        for ($y = 0; $y -lt $height * $Scale; $y++) {
            if ($x -ge $sourceWidth * $Scale) { $data.Add(0); continue }
            $index = [int]$source[[int]($y / $Scale) * $sourceWidth + [int]($x / $Scale)]
            $mapped = [byte]0
            if ($index -lt $paletteMap.Length) { $mapped = $paletteMap[$index] }
            $data.Add($mapped)
        }
    }
}

$parent = Split-Path -Parent $OutputFont
if ($parent) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
$stream = [IO.File]::Open($OutputFont, [IO.FileMode]::Create)
try {
    $stream.Write($header, 0, $header.Length)
    $stream.Write($data.ToArray(), 0, $data.Count)
} finally { $stream.Dispose() }
Write-Host "Converted TinyBaby font: $OutputFont"
