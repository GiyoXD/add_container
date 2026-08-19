@echo off
echo ====================================================
echo   Packaging Standalone Staff Executable (View-Only)
echo ====================================================

set PATH=C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.11.0\mingw_64\bin;%PATH%
set QT_PATH=C:/Qt/6.11.0/mingw_64
set STAFF_EXE=build_staff\add_container.exe
set DIST_DIR=dist_app

echo Cleaning previous staff build...
if exist "build_staff" rmdir /s /q "build_staff"
if exist "%DIST_DIR%" rmdir /s /q "%DIST_DIR%"
mkdir "%DIST_DIR%"

echo Compiling Staff View-Only Binary (ENABLE_CROSS_BORDER=OFF)...
cmake -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="%QT_PATH%" -B build_staff -DENABLE_CROSS_BORDER=OFF
cmake --build build_staff

if not exist "%STAFF_EXE%" (
    echo [!] Build failed!
    pause
    exit /b 1
)

echo Copying staff executable to dist folder...
copy "%STAFF_EXE%" "%DIST_DIR%\"

if exist "shipping_data.db" (
    copy "shipping_data.db" "%DIST_DIR%\"
)
if exist "config.ini" (
    copy "config.ini" "%DIST_DIR%\"
)
if exist "secret.json" (
    copy "secret.json" "%DIST_DIR%\"
)

echo Deploying Qt & Compiler Runtime DLLs...
"C:\Qt\6.11.0\mingw_64\bin\windeployqt.exe" --compiler-runtime "%DIST_DIR%\add_container.exe"

echo.
echo ====================================================
echo   SUCCESS! Standalone Staff folder created at: %DIST_DIR%
echo   This build is 100%% View-Only and CANNOT be unlocked by editing config.ini!
echo ====================================================
pause
