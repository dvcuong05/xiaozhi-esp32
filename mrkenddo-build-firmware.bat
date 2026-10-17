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

:: Nap token WSS tu secret file cua repo MCP anh em neu chua duoc truyen tu ngoai.
if not defined XIAOZHI_MCP_DEVICE_WSS_TOKEN if not defined XIAOZHI_MCP_DEVICE_WSS_TOKEN_FILE set "XIAOZHI_MCP_DEVICE_WSS_TOKEN_FILE=%~dp0..\xiaozhi-kenddo-services\secrets\xiaozhi_device_bridge_token"
if not defined XIAOZHI_MCP_DEVICE_WSS_TOKEN if not exist "%XIAOZHI_MCP_DEVICE_WSS_TOKEN_FILE%" (
    echo [LOI] Khong tim thay secret token WSS.
    echo Hay dat XIAOZHI_MCP_DEVICE_WSS_TOKEN_FILE hoac kiem tra repo xiaozhi-kenddo-services.
    pause
    exit /b 1
)

if not exist "%IDF_EXPORT%" (
    echo [LOI] Khong tim thay file: %IDF_EXPORT%
    echo Vui long mo file .bat va sua lai dung thu muc esp-idf tren may ban!
    pause
    exit /b 1
)
call "%IDF_EXPORT%"

:: 3. Chay lenh build XiaoZhi cho board robot
echo ========================================================
echo Dang thuc thi build.py cho board mrkenddo-robot...
echo ========================================================
python scripts\build.py mrkenddo-robot --language vi-VN --wake-word wn9_hilili_tts

if %ERRORLEVEL% equ 0 (
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
