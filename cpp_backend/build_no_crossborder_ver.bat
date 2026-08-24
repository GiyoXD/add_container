@echo off
echo ====================================================
echo   Building Restricted Version (Staff - View Only)
echo ====================================================

taskkill /IM add_container.exe /F 2>nul

set PATH=C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.11.0\mingw_64\bin;%PATH%
set QT_PATH=C:/Qt/6.11.0/mingw_64

if exist "build_staff" rmdir /s /q "build_staff"
cmake -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="%QT_PATH%" -B build_staff -DENABLE_CROSS_BORDER=OFF
cmake --build build_staff

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [!] Staff Build FAILED. Check errors above.
    pause
    exit /b %ERRORLEVEL%
)

echo Copying secret.json to build_staff folder...
if exist "..\backend\secret.json" (
    copy "..\backend\secret.json" "build_staff\"
) else if exist "secret.json" (
    copy "secret.json" "build_staff\"
)

echo Creating config.ini in build_staff folder...
(
  echo [Credentials]
  echo SpreadsheetId=1piQv1mpEWWBw3hUITAD2Q5ZHdvnqP38n0OLUbK5Y1Hk
  echo GoogleSecretFile=secret.json
) > build_staff\config.ini

echo Deploying Qt & Compiler Runtime DLLs (Qt6Network.dll, Qt6Core.dll, etc.) into build_staff...
"C:\Qt\6.11.0\mingw_64\bin\windeployqt.exe" --compiler-runtime "build_staff\add_container.exe"

echo.
echo ====================================================
echo   SUCCESS! Staff View-Only Standalone build complete.
echo   Output folder: build_staff\ (Contains add_container.exe, all DLLs, secret.json, config.ini)
echo ====================================================
pause
