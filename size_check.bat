@echo off
setlocal enabledelayedexpansion

set LIMIT=1474560
set TOTAL=0

if not exist dist (
    echo ERROR: dist folder not found.
    exit /b 1
)

for /R dist %%A in (*) do (
    set /A TOTAL+=%%~zA
)

echo dist size: !TOTAL! bytes
echo limit: %LIMIT% bytes

if !TOTAL! LEQ %LIMIT% (
    echo OK
    exit /b 0
) else (
    echo FAILED
    exit /b 1
)

