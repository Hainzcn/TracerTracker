@echo off
chcp 65001 >nul
setlocal EnableDelayedExpansion

echo ============================================================
echo   TracerTracker - Build Script
echo ============================================================
echo.

set "PROJECT_DIR=%~dp0"
set "BUILD_DIR=%PROJECT_DIR%build"
set "EXE_PATH=%BUILD_DIR%\TracerTracker.exe"

:: ── Step 1: CMake Configure ──────────────────────────────────

echo [1/5] CMake Configure (MinGW Makefiles, Release)...
cmake -B "%BUILD_DIR%" -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -S "%PROJECT_DIR%"
if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] CMake configure failed.
    pause
    exit /b 1
)
echo      Done.
echo.

:: ── Step 2: Build ────────────────────────────────────────────

echo [2/5] Compiling (parallel jobs: %NUMBER_OF_PROCESSORS%)...
cmake --build "%BUILD_DIR%" --config Release -j %NUMBER_OF_PROCESSORS%
if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] Build failed.
    pause
    exit /b 1
)
echo      Done.
echo.

:: ── Step 3: Deploy Qt dependencies ──────────────────────────

echo [3/5] Deploying Qt runtime dependencies (windeployqt6)...
where windeployqt6 >nul 2>&1
if %ERRORLEVEL% equ 0 (
    windeployqt6 "%EXE_PATH%"
    if !ERRORLEVEL! neq 0 (
        echo      [WARN] windeployqt6 returned errors, some DLLs may be missing.
    ) else (
        echo      Done.
    )
) else (
    where windeployqt >nul 2>&1
    if !ERRORLEVEL! equ 0 (
        windeployqt "%EXE_PATH%"
        echo      Done (via windeployqt).
    ) else (
        echo      [WARN] windeployqt not found in PATH, skipping Qt deployment.
        echo             You may need to manually copy Qt DLLs to the build directory.
    )
)
echo.

:: ── Step 4: Copy config.json ─────────────────────────────────

echo [4/5] Copying config.json...
if not exist "%BUILD_DIR%\config.json" (
    copy "%PROJECT_DIR%config.json" "%BUILD_DIR%\config.json" >nul
    echo      Copied config.json to build directory.
) else (
    echo      config.json already exists, skipped.
)
echo.

:: ── Step 5: Create shortcut in project directory ─────────────

set "SHORTCUT_PATH=%PROJECT_DIR%TracerTracker.lnk"
echo [5/5] Creating shortcut...
powershell -NoProfile -Command ^
    "$ws = New-Object -ComObject WScript.Shell;" ^
    "$shortcut = $ws.CreateShortcut('%SHORTCUT_PATH%');" ^
    "$shortcut.TargetPath = '%EXE_PATH%';" ^
    "$shortcut.WorkingDirectory = '%BUILD_DIR%';" ^
    "$shortcut.Description = 'TracerTracker - 3D Trajectory Visualizer';" ^
    "$shortcut.Save()"
if %ERRORLEVEL% equ 0 (
    echo      Shortcut created: %SHORTCUT_PATH%
) else (
    echo      [WARN] Failed to create shortcut.
)
echo.

:: ── Done ─────────────────────────────────────────────────────

echo ============================================================
echo   Build completed successfully!
echo.
echo   Executable:  %EXE_PATH%
echo   Shortcut:    %SHORTCUT_PATH%
echo ============================================================
echo.
pause
