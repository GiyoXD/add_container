@echo off
echo ====================================================
echo   Building Admin Version (With Full Features)
echo ====================================================

taskkill /IM add_container.exe /F 2>nul

set PATH=C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.11.0\mingw_64\bin;%PATH%
set QT_PATH=C:/Qt/6.11.0/mingw_64

if exist "build_admin" rmdir /s /q "build_admin"
cmake -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="%QT_PATH%" -B build_admin -DENABLE_CROSS_BORDER=ON
cmake --build build_admin

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [!] Admin Build FAILED. Check errors above.
    pause
    exit /b %ERRORLEVEL%
)

echo Copying secret.json to build_admin folder...
if exist "..\backend\secret.json" (
    copy "..\backend\secret.json" "build_admin\"
) else if exist "secret.json" (
    copy "secret.json" "build_admin\"
)

echo Creating config.ini in build_admin folder...
(
  echo [Credentials]
  echo SpreadsheetId=1piQv1mpEWWBw3hUITAD2Q5ZHdvnqP38n0OLUbK5Y1Hk
  echo GoogleSecretFile=secret.json
) > build_admin\config.ini

echo Deploying Qt & Compiler Runtime DLLs (Qt6Network.dll, Qt6Core.dll, etc.) into build_admin...
"C:\Qt\6.11.0\mingw_64\bin\windeployqt.exe" --compiler-runtime "build_admin\add_container.exe"

echo.
echo ====================================================
echo   SUCCESS! Admin Standalone build complete.
echo   Output folder: build_admin\ (Contains add_container.exe, all DLLs, secret.json, config.ini)
echo ====================================================
pause
