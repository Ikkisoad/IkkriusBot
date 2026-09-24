@echo off
setlocal
set "BOT_DIR=%~dp0"
set "STARCRAFT_DIR=%~dp0..\starcraft"

if exist "%BOT_DIR%updated\IkkriusBot.exe" (
    powershell -NoProfile -Command "$ErrorActionPreference = 'Stop'; $source = Get-Item -LiteralPath (Join-Path $env:BOT_DIR 'updated\IkkriusBot.exe'); $target = Join-Path $env:BOT_DIR 'IkkriusBot.exe'; if (!(Test-Path -LiteralPath $target) -or $source.LastWriteTimeUtc -gt (Get-Item -LiteralPath $target).LastWriteTimeUtc) { Copy-Item -LiteralPath $source.FullName -Destination $target -Force }"
    if errorlevel 1 (
        echo ERROR: Could not install the updated bot. Close the running IkkriusBot and launch again.
        goto :failed
    )
)

if not exist "%BOT_DIR%IkkriusBot.exe" (
    echo ERROR: IkkriusBot.exe is missing from "%BOT_DIR%".
    echo Build the bot in Visual Studio before running this launcher.
    goto :failed
)

for %%F in (RunStarcraftWithBWAPI.bat StarCraft.exe injectory_x86.exe bwapi-data\BWAPI.dll bwapi-data\bwapi.ini WMode.dll) do (
    if not exist "%STARCRAFT_DIR%\%%F" (
        echo ERROR: Missing "%STARCRAFT_DIR%\%%F".
        echo Extract the StarCraft + BWAPI 4.4.0 bundle into the starcraft folder.
        echo See the Setup section in README.md for the download link.
        goto :failed
    )
)

start "IkkriusBot" /D "%BOT_DIR%" "%BOT_DIR%IkkriusBot.exe"
if errorlevel 1 goto :failed
start "StarCraft with BWAPI" /D "%STARCRAFT_DIR%" cmd /d /c "call RunStarcraftWithBWAPI.bat || pause"
if errorlevel 1 goto :failed
exit /b 0

:failed
pause
exit /b 1
