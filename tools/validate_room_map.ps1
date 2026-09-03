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
function Read-WholeTile([string]$value, [string]$label) {
    $number = [double]::Parse(($value.Trim() -replace 'f$', ''), [Globalization.CultureInfo]::InvariantCulture)
    if ($number -ne [math]::Truncate($number)) { throw "$label is not a whole tile: $value" }
    return [int]$number
}

$mapTiles = [System.Collections.Generic.HashSet[string]]::new()
for ($y = 0; $y -lt $rows.Count; $y++) {
    for ($x = 0; $x -lt $rows[$y].Length; $x++) {
        if ($rows[$y][$x] -eq '#') { Add-Tile $mapTiles $x $y }
    }
}

$codeText = Get-Content -Raw -LiteralPath $codePath
$arrayPattern = "(?s)static const RectF g_room${roomId}_platforms\[\]\s*=\s*\{(.*?)\n\};"
$arrayMatch = [regex]::Match($codeText, $arrayPattern)
if (!$arrayMatch.Success) { throw "g_room${roomId}_platforms 배열을 찾을 수 없습니다." }
$rectPattern = '\{\s*T\(([^)]+)\),\s*T\(([^)]+)\),\s*T\(([^)]+)\),\s*T\(([^)]+)\)\s*\}'
$rects = [regex]::Matches($arrayMatch.Groups[1].Value, $rectPattern)
if ($rects.Count -eq 0) { throw '플랫폼 Rect를 찾을 수 없습니다.' }

$codeTiles = [System.Collections.Generic.HashSet[string]]::new()
foreach ($rect in $rects) {
    $x = Read-WholeTile $rect.Groups[1].Value 'platform x'
    $y = Read-WholeTile $rect.Groups[2].Value 'platform y'
    $w = Read-WholeTile $rect.Groups[3].Value 'platform width'
    $h = Read-WholeTile $rect.Groups[4].Value 'platform height'
    for ($tileY = $y; $tileY -lt ($y + $h); $tileY++) {
        for ($tileX = $x; $tileX -lt ($x + $w); $tileX++) { Add-Tile $codeTiles $tileX $tileY }
    }
}

$missing = @($mapTiles | Where-Object { !$codeTiles.Contains($_) } | Sort-Object)
$extra = @($codeTiles | Where-Object { !$mapTiles.Contains($_) } | Sort-Object)
Write-Output ("ROOM {0}: ASCII #={1}, code #={2}, missing={3}, extra={4}" -f $roomId, $mapTiles.Count, $codeTiles.Count, $missing.Count, $extra.Count)
if ($missing.Count -or $extra.Count) {
    if ($missing.Count) { Write-Output ('missing: ' + ($missing -join ' ')) }
    if ($extra.Count) { Write-Output ('extra: ' + ($extra -join ' ')) }
    exit 1
}
