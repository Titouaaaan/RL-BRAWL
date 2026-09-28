@echo off
rem play.bat: builds the C++ game code (only what changed) and starts the game.
rem   play.bat        -> play the game
rem   play.bat edit   -> open the project in the Godot editor instead
rem Double-clicking this file in Explorer also works.
setlocal
cd /d "%~dp0"

rem 1. Fetch godot-cpp if this is a fresh clone (git submodule).
if not exist extern\godot-cpp\CMakeLists.txt (
    echo [play] Downloading godot-cpp...
    git submodule update --init || goto :error
)

rem 2. Build the extension DLL. The first build takes about a minute; after
rem    that, only changed files are recompiled (a few seconds).
if not exist build\godot\CMakeCache.txt (
    echo [play] Configuring the build...
    python -m cmake -S bindings/godot -B build/godot || goto :error
)
rem    Windows won't let the build overwrite a DLL that a running Godot has
rem    loaded, but it does allow renaming it. So we move the old one aside;
rem    an open game keeps running the old code, a new one gets the new code.
set "DLL=godot\bin\fightgame.windows.template_debug.x86_64.dll"
if exist "%DLL%.old" del "%DLL%.old" 2>nul
if exist "%DLL%" move /y "%DLL%" "%DLL%.old" >nul 2>&1
echo [play] Building C++...
python -m cmake --build build/godot --config Release --parallel -- /nologo /verbosity:minimal || goto :error

rem 3. Find Godot: the GODOT environment variable if set, otherwise search Downloads.
if not defined GODOT (
    for /f "delims=" %%G in ('where /r "%USERPROFILE%\Downloads" Godot_v4*_win64.exe 2^>nul') do (
        if not defined GODOT set "GODOT=%%G"
    )
)
if not defined GODOT (
    echo [play] Could not find Godot. Set the GODOT environment variable to Godot's .exe path.
    goto :error
)

rem 4. The first time, let Godot scan the project so it registers our extension.
if not exist godot\.godot\extension_list.cfg (
    echo [play] First run: importing the Godot project...
    "%GODOT%" --headless --path godot --import >nul 2>&1
)

if /i "%~1"=="edit" (
    start "" "%GODOT%" --path godot -e
) else (
    echo [play] Starting the game. Esc quits.
    "%GODOT%" --path godot
)
exit /b 0

:error
echo.
echo [play] FAILED - see the messages above. docs/running.md has fixes for common errors.
pause
exit /b 1
