@echo off
rem play.bat: builds the C++ game code (only what changed) and starts the game.
rem   play.bat        -> play the game
rem   play.bat edit   -> open the project in the Godot editor instead
rem   play.bat replay [file.json] -> watch a recorded game (default: the newest one)
rem   add --p1 ID --p2 ID to any of these to pick the characters (ids: assets\characters\characters.json)
rem   play.bat --vs [bot] -> fight a bot as P1: 'latest' (default), a checkpoint .pt, or fighter/spacer/jabber/...
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

rem 5. Read the options (any order): edit, replay [file.json], --p1 ID, --p2 ID.
rem    The character ids are listed in assets\characters\characters.json.
set "MODE=play"
set "RFILE="
set "CHARS="
set "VS="
:args
if "%~1"=="" goto :run
if /i "%~1"=="edit" (set "MODE=edit" & shift & goto :args)
if /i "%~1"=="replay" (set "MODE=replay" & shift & goto :args)
if /i "%~1"=="--p1" (set "CHARS=%CHARS% --p1=%~2" & shift & shift & goto :args)
if /i "%~1"=="--p2" (set "CHARS=%CHARS% --p2=%~2" & shift & shift & goto :args)
if /i "%~1"=="latest" (set "RFILE=latest" & shift & goto :args)
if /i "%~1"=="--vs" goto :vs
set "RFILE=%~f1"
shift
goto :args

:vs
rem --vs takes an optional bot name: the next argument, unless it is another option.
set "VS=latest"
set "NEXT=%~2"
if not "%NEXT%"=="" if not "%NEXT:~0,2%"=="--" (set "VS=%~2" & shift)
shift
goto :args

:run
if "%MODE%"=="edit" (
    start "" "%GODOT%" --path godot -e
    exit /b 0
)
if "%MODE%"=="replay" if "%RFILE%"=="" set "RFILE=latest"
if "%MODE%"=="replay" (
    echo [play] Replay %RFILE%. Space pauses, Up/Down change speed.
    "%GODOT%" --path godot -- "--replay=%RFILE%" %CHARS%
) else if defined VS (
    rem The bot runs in Python, train\serve.py, in its own minimized window,
    rem and closes by itself when the game does. No brackets in these comments:
    rem a closing bracket would end this block early.
    echo [play] Starting the bot "%VS%" and the game. You are P1. Esc quits.
    start "bot: %VS%" /min python train\serve.py "%VS%" --port 5055
    "%GODOT%" --path godot -- --vs=5055 %CHARS%
) else (
    echo [play] Starting the game. Esc quits.
    "%GODOT%" --path godot -- %CHARS%
)
exit /b 0

:error
echo.
echo [play] FAILED - see the messages above. docs/running.md has fixes for common errors.
pause
exit /b 1
