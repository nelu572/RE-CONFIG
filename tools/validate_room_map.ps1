param(
    [Parameter(Mandatory = $true)]
    [ValidateRange(0, 99)]
    [int]$Room
)

$roomId = $Room.ToString('00')
$mapPath = Join-Path $PSScriptRoot "..\docs\rooms\ROOM_${roomId}_MAP.md"
$codePath = Join-Path $PSScriptRoot "..\src\game\rooms\room${roomId}.cpp"
if (!(Test-Path -LiteralPath $mapPath)) { throw "맵 문서를 찾을 수 없습니다: $mapPath" }
if (!(Test-Path -LiteralPath $codePath)) { throw "ROOM 코드를 찾을 수 없습니다: $codePath" }

$mapText = Get-Content -Raw -LiteralPath $mapPath
$mapBlock = [regex]::Match($mapText, '(?s)~~~text\s*\r?\n(.*?)\r?\n~~~')
if (!$mapBlock.Success) { throw 'ROOM 맵의 ~~~text 블록을 찾을 수 없습니다.' }
$rows = @($mapBlock.Groups[1].Value -split "`r?`n")
if ($rows.Count -eq 0 -or ($rows | Where-Object { $_.Length -ne $rows[0].Length })) {
    throw 'ASCII 맵의 행 길이가 일치하지 않습니다.'
}

function Add-Tile([System.Collections.Generic.HashSet[string]]$set, [int]$x, [int]$y) {
    [void]$set.Add("${x},${y}")
}
function Add-Run([System.Collections.Generic.HashSet[string]]$set, [int]$x, [int]$y, [int]$length) {
    [void]$set.Add("$($x),$($y),$($length)")
}
function Read-TileNumber([string]$value) {
    return [double]::Parse(($value.Trim() -replace 'f$', ''), [Globalization.CultureInfo]::InvariantCulture)
}
function Read-WholeTile([string]$value, [string]$label) {
    $number = Read-TileNumber $value
    if ($number -ne [math]::Truncate($number)) { throw "$label is not a whole tile: $value" }
    return [int]$number
}
function Get-HorizontalRuns($mapRows, [char]$marker) {
    $runs = [System.Collections.Generic.HashSet[string]]::new()
    for ($y = 0; $y -lt $mapRows.Count; $y++) {
        $x = 0
        while ($x -lt $mapRows[$y].Length) {
            if ($mapRows[$y][$x] -ne $marker) { $x++; continue }
            $start = $x
            while ($x -lt $mapRows[$y].Length -and $mapRows[$y][$x] -eq $marker) { $x++ }
            Add-Run $runs $start $y ($x - $start)
        }
    }
    return $runs
}
function Get-VerticalRuns($mapRows, [char]$marker) {
    $remaining = [System.Collections.Generic.HashSet[string]]::new()
    for ($y = 0; $y -lt $mapRows.Count; $y++) {
        for ($x = 0; $x -lt $mapRows[$y].Length; $x++) {
            if ($mapRows[$y][$x] -eq $marker) { Add-Tile $remaining $x $y }
        }
    }
    $runs = [System.Collections.Generic.HashSet[string]]::new()
    while ($remaining.Count) {
        $seed = @($remaining | ForEach-Object { $parts = $_.Split(','); @{ x = [int]$parts[0]; y = [int]$parts[1] } } | Sort-Object y, x)[0]
        $height = 0
        while ($remaining.Remove("$($seed.x),$($seed.y + $height)")) { $height++ }
        Add-Run $runs $seed.x $seed.y $height
    }
    return $runs
}

$mapTiles = [System.Collections.Generic.HashSet[string]]::new()
for ($y = 0; $y -lt $rows.Count; $y++) {
    for ($x = 0; $x -lt $rows[$y].Length; $x++) {
        if ($rows[$y][$x] -eq '#') { Add-Tile $mapTiles $x $y }
    }
}

$mapTopBlocks = Get-HorizontalRuns $rows '='
$mapLeftBlocks = Get-VerticalRuns $rows '|'

$mapStaticSpikes = [System.Collections.Generic.HashSet[string]]::new()
for ($y = 0; $y -lt $rows.Count; $y++) {
    for ($x = 0; $x -lt $rows[$y].Length; $x++) {
        $marker = [string]$rows[$y][$x]
        if ($marker -eq 'M' -and $x + 1 -lt $rows[$y].Length -and $rows[$y][$x + 1] -match '[12]') { continue }
        $rotation = $null
        if ($marker -eq 'M') { $rotation = 'STATIC_SPIKE_ROTATION_0_DEGREES' }
        elseif ($marker -eq 'N') { $rotation = 'STATIC_SPIKE_ROTATION_180_DEGREES' }
        elseif ($marker -eq 'L') { $rotation = 'STATIC_SPIKE_ROTATION_90_DEGREES' }
        elseif ($marker -eq 'R') { $rotation = 'STATIC_SPIKE_ROTATION_270_DEGREES' }
        if ($null -ne $rotation) {
            [void]$mapStaticSpikes.Add("$x,$y,$rotation")
        }
    }
}

$codeText = Get-Content -Raw -LiteralPath $codePath
$codeStaticSpikes = [System.Collections.Generic.HashSet[string]]::new()
$staticSpikeArrayPattern = "(?s)static const StaticSpikeDef g_room${roomId}_static_spikes\[\]\s*=\s*\{(.*?)\n\};"
$staticSpikeArray = [regex]::Match($codeText, $staticSpikeArrayPattern)
if ($staticSpikeArray.Success) {
    $staticSpikePattern = 'StaticSpikeAt\(T\(([^)]+)\),\s*T\(([^)]+)\),\s*(STATIC_SPIKE_ROTATION_(?:0|90|180|270)_DEGREES)\)'
    foreach ($spike in [regex]::Matches($staticSpikeArray.Groups[1].Value, $staticSpikePattern)) {
        $x = Read-WholeTile $spike.Groups[1].Value 'static spike x'
        $y = Read-WholeTile $spike.Groups[2].Value 'static spike y'
        [void]$codeStaticSpikes.Add("$x,$y,$($spike.Groups[3].Value)")
    }
}
$staticMissing = @($mapStaticSpikes | Where-Object { !$codeStaticSpikes.Contains($_) } | Sort-Object)
$staticExtra = @($codeStaticSpikes | Where-Object { !$mapStaticSpikes.Contains($_) } | Sort-Object)
$staticArrayMissing = $mapStaticSpikes.Count -gt 0 -and !$staticSpikeArray.Success
$staticUnparsed = $staticSpikeArray.Success -and $mapStaticSpikes.Count -gt 0 -and $codeStaticSpikes.Count -eq 0
$staticError = $staticArrayMissing -or $staticUnparsed -or $staticMissing.Count -or $staticExtra.Count
$arrayPattern = "(?s)static const RectF g_room${roomId}_platforms\[\]\s*=\s*\{(.*?)\n\};"
$arrayMatch = [regex]::Match($codeText, $arrayPattern)
if (!$arrayMatch.Success) { throw "g_room${roomId}_platforms 배열을 찾을 수 없습니다." }
$rectPattern = '\{\s*T\(([^)]+)\),\s*T\(([^)]+)\),\s*T\(([^)]+)\),\s*T\(([^)]+)\)\s*\}'
$rects = [regex]::Matches($arrayMatch.Groups[1].Value, $rectPattern)
if ($rects.Count -eq 0) { throw '플랫폼 Rect를 찾을 수 없습니다.' }

$codeTiles = [System.Collections.Generic.HashSet[string]]::new()
$codeTopBlocks = [System.Collections.Generic.HashSet[string]]::new()
$codeLeftBlocks = [System.Collections.Generic.HashSet[string]]::new()
foreach ($rect in $rects) {
    $x = Read-WholeTile $rect.Groups[1].Value 'platform x'
    $y = Read-WholeTile $rect.Groups[2].Value 'platform y'
    $wNumber = Read-TileNumber $rect.Groups[3].Value
    $hNumber = Read-TileNumber $rect.Groups[4].Value
    if ($wNumber -eq [math]::Truncate($wNumber) -and $hNumber -eq [math]::Truncate($hNumber)) {
        $w = [int]$wNumber; $h = [int]$hNumber
        for ($tileY = $y; $tileY -lt ($y + $h); $tileY++) {
            for ($tileX = $x; $tileX -lt ($x + $w); $tileX++) { Add-Tile $codeTiles $tileX $tileY }
        }
    } elseif ($wNumber -eq [math]::Truncate($wNumber) -and $hNumber -eq 0.25) {
        Add-Run $codeTopBlocks $x $y ([int]$wNumber)
    } elseif ($wNumber -eq 0.25 -and $hNumber -eq [math]::Truncate($hNumber)) {
        Add-Run $codeLeftBlocks $x $y ([int]$hNumber)
    } else {
        throw "지원하지 않는 부분 플랫폼 Rect: $($rect.Value)"
    }
}

$missing = @($mapTiles | Where-Object { !$codeTiles.Contains($_) } | Sort-Object)
$extra = @($codeTiles | Where-Object { !$mapTiles.Contains($_) } | Sort-Object)
$topMissing = @($mapTopBlocks | Where-Object { !$codeTopBlocks.Contains($_) } | Sort-Object)
$topExtra = @($codeTopBlocks | Where-Object { !$mapTopBlocks.Contains($_) } | Sort-Object)
$leftMissing = @($mapLeftBlocks | Where-Object { !$codeLeftBlocks.Contains($_) } | Sort-Object)
$leftExtra = @($codeLeftBlocks | Where-Object { !$codeLeftBlocks.Contains($_) } | Sort-Object)
Write-Output ("ROOM {0}: ASCII #={1}, code #={2}, missing={3}, extra={4}; = missing={5}, extra={6}; | missing={7}, extra={8}; static spikes ASCII={9}, code={10}, missing={11}, extra={12}" -f $roomId, $mapTiles.Count, $codeTiles.Count, $missing.Count, $extra.Count, $topMissing.Count, $topExtra.Count, $leftMissing.Count, $leftExtra.Count, $mapStaticSpikes.Count, $codeStaticSpikes.Count, $staticMissing.Count, $staticExtra.Count)
if ($missing.Count -or $extra.Count -or $topMissing.Count -or $topExtra.Count -or $leftMissing.Count -or $leftExtra.Count -or $staticError) {
    if ($missing.Count) { Write-Output ('# missing: ' + ($missing -join ' ')) }
    if ($extra.Count) { Write-Output ('# extra: ' + ($extra -join ' ')) }
    if ($topMissing.Count) { Write-Output ('= missing: ' + ($topMissing -join ' ')) }
    if ($topExtra.Count) { Write-Output ('= extra: ' + ($topExtra -join ' ')) }
    if ($leftMissing.Count) { Write-Output ('| missing: ' + ($leftMissing -join ' ')) }
    if ($leftExtra.Count) { Write-Output ('| extra: ' + ($leftExtra -join ' ')) }
    if ($staticArrayMissing) { Write-Output 'static spike array missing' }
    if ($staticUnparsed) { Write-Output 'static spike array has no readable entries' }
    if ($staticMissing.Count) { Write-Output ('static spike missing: ' + ($staticMissing -join ' ')) }
    if ($staticExtra.Count) { Write-Output ('static spike extra: ' + ($staticExtra -join ' ')) }
    exit 1
}
