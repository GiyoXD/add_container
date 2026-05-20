@echo off
echo =======================================
echo   Building AddContainerProject...
echo =======================================

:: Run the build
cmake --build build_qt6

:: Check if build succeeded
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [!] Build FAILED. Check errors above.
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo [!] Build SUCCESSFUL. Starting app...
echo.

:: Run the app
start "" "build_qt6\add_container.exe"
