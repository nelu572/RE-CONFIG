@echo off
setlocal

where cl.exe >nul 2>nul
if errorlevel 1 (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "%VSWHERE%" (
        for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
        if defined VSINSTALL if exist "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" (
            call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
        )
    )
)

where cl.exe >nul 2>nul
if errorlevel 1 (
    if exist "%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" (
        call "%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
    ) else if exist "%ProgramFiles%\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
        call "%ProgramFiles%\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
    ) else if exist "%ProgramFiles%\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" (
        call "%ProgramFiles%\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul
    ) else if exist "%ProgramFiles%\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
        call "%ProgramFiles%\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul
    ) else if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
        call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
    ) else if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
        call "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
    ) else if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
        call "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul
    ) else if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
        call "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" >nul
    ) else (
        echo ERROR: cl.exe not found. Install Visual Studio C++ Build Tools.
        exit /b 1
    )
)

if not exist build mkdir build
if not exist dist mkdir dist

set CFLAGS=/nologo /std:c++17 /utf-8 /O1 /Os /Oi /GS- /GR- /Gw /Gy /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DSTB_VORBIS_NO_STDIO /DSTB_VORBIS_NO_PUSHDATA_API /DSTB_VORBIS_MAX_CHANNELS=2 /DSTB_VORBIS_FAST_HUFFMAN_LENGTH=7 /DNDEBUG /Isrc\core /Isrc\platform /Isrc\render /Isrc\game /Isrc\ui
set LFLAGS=/link /SUBSYSTEM:WINDOWS /NODEFAULTLIB /ENTRY:WinMainCRTStartup /INCREMENTAL:NO /OPT:REF /OPT:ICF kernel32.lib user32.lib gdi32.lib winmm.lib msvcrt.lib

where cl.exe >nul 2>nul
if errorlevel 1 goto mingw_build
if "%INCLUDE%"=="" goto mingw_build
if not exist "%VCToolsInstallDir%include\stddef.h" goto mingw_build
if not exist "%WindowsSdkDir%Include\%WindowsSDKVersion%um\windows.h" goto mingw_build

cl %CFLAGS% /Febuild\reconfig.exe /Fobuild\ src\main.cpp src\core\math_util.cpp src\core\perf.cpp src\platform\input.cpp src\platform\audio.cpp src\platform\stb_vorbis.c src\render\camera.cpp src\render\render.cpp src\render\framebuffer.cpp src\render\stage_render.cpp src\render\stage_cache.cpp src\game\world.cpp src\game\rooms\room00.cpp src\game\rooms\room01.cpp src\game\rooms\room02.cpp src\game\rooms\room03.cpp src\game\rooms\room04.cpp src\game\delete_rules.cpp src\game\player.cpp src\game\collision.cpp src\game\player_movement.cpp src\game\exit_sequence.cpp src\game\stage_update.cpp src\ui\main_menu.cpp src\ui\modal_ui.cpp src\ui\pause_menu.cpp src\ui\ui_text.cpp src\ui\ui_text_small.cpp src\ui\settings_ui.cpp src\ui\tutorial_ui.cpp %LFLAGS%
if errorlevel 1 goto mingw_build
goto copy_dist

:mingw_build
where g++.exe >nul 2>nul
if errorlevel 1 exit /b 1

g++.exe -std=c++17 -Os -s ^
    -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DSTB_VORBIS_NO_STDIO -DSTB_VORBIS_NO_PUSHDATA_API -DSTB_VORBIS_MAX_CHANNELS=2 -DSTB_VORBIS_FAST_HUFFMAN_LENGTH=7 -DNDEBUG ^
    -Isrc\core -Isrc\platform -Isrc\render -Isrc\game -Isrc\ui ^
    src\main.cpp src\core\math_util.cpp src\core\perf.cpp src\platform\input.cpp ^
    src\platform\audio.cpp src\platform\stb_vorbis.c ^
    src\render\camera.cpp src\render\render.cpp src\render\framebuffer.cpp ^
    src\render\stage_render.cpp src\render\stage_cache.cpp ^
    src\game\world.cpp src\game\rooms\room00.cpp src\game\rooms\room01.cpp src\game\rooms\room02.cpp src\game\rooms\room03.cpp src\game\rooms\room04.cpp src\game\delete_rules.cpp src\game\player.cpp ^
    src\game\collision.cpp src\game\player_movement.cpp src\game\exit_sequence.cpp ^
    src\game\stage_update.cpp src\ui\ui_text.cpp src\ui\ui_text_small.cpp ^
    src\ui\main_menu.cpp src\ui\modal_ui.cpp src\ui\pause_menu.cpp src\ui\settings_ui.cpp src\ui\tutorial_ui.cpp ^
    -mwindows -nostdlib -Wl,-e,WinMainCRTStartup ^
    -lkernel32 -luser32 -lgdi32 -lwinmm -lmsvcrt -lm -lgcc -o build\reconfig.exe
if errorlevel 1 exit /b 1

:copy_dist
copy /Y build\reconfig.exe dist\reconfig.exe >nul
if exist dist\assets rmdir /S /Q dist\assets
mkdir dist\assets\audio
copy /Y assets\audio\bgm_main.ogg dist\assets\audio\bgm_main.ogg >nul
copy /Y assets\audio\sfx_jump.ogg dist\assets\audio\sfx_jump.ogg >nul
copy /Y assets\audio\sfx_death.ogg dist\assets\audio\sfx_death.ogg >nul
call size_check.bat
endlocal
exit /b 0
