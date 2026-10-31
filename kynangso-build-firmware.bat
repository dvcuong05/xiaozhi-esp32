@echo off
chcp 65001 > nul
echo ========================================================
echo Dang khoi tao moi truong ESP-IDF va build firmware...
echo ========================================================

:: 1. Chuyen ve thu muc chua file bat (thu muc goc repo)
cd /d "%~dp0"

:: 2. Nap moi truong ESP-IDF (dung lenh call de khong bi thoat script)
:: Kiem tra dung thu muc version ESP-IDF thuc te trong C:\Espressif\frameworks\
set "IDF_EXPORT=C:\esp\v6.1\esp-idf\export.bat"

:: Firmware chung: provision WSS qua USB sau khi nap. Chi dung legacy token khi
:: nguoi goi chu dong dat XIAOZHI_MCP_DEVICE_WSS_TOKEN hoac *_TOKEN_FILE.

if not exist "%IDF_EXPORT%" (
    echo [LOI] Khong tim thay file: %IDF_EXPORT%
    echo Vui long mo file .bat va sua lai dung thu muc esp-idf tren may ban!
    pause
    exit /b 1
)
call "%IDF_EXPORT%"
if errorlevel 1 exit /b %ERRORLEVEL%

:: 3. Chay lenh build XiaoZhi cho board robot
echo ========================================================
echo Dang thuc thi build.py cho board kynangso-robot...
echo ========================================================
python scripts\build.py kynangso-robot --language vi-VN --wake-word wn9_hilili_tts
set "BUILD_RESULT=%ERRORLEVEL%"

if %BUILD_RESULT% equ 0 (
    echo.
    echo ========================================================
    echo [THANH CONG] Da build xong firmware!
    echo ========================================================
) else (
    echo.
    echo ========================================================
    echo [THAT BAI] Qua trinh build xay ra loi!
    echo ========================================================
)

pause
exit /b %BUILD_RESULT%
