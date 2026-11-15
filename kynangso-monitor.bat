@echo off
chcp 65001 > nul
title XiaoZhi ESP32 - Firmware Flash and Monitor
echo ========================================================
echo        XIAOZHI ESP32 - FIRMWARE FLASH AND MONITOR
echo ========================================================

:: 1. Chuyen ve thu muc goc repo
cd /d "%~dp0"

:: 2. Nap moi truong ESP-IDF 6.1 (theo dung duong dan tren may cua ban)
set "IDF_EXPORT=C:\esp\v6.1\esp-idf\export.bat"
set "IDF_TOOLS_PATH=C:\Espressif"
set "PYTHONUTF8=1"

if not exist "%IDF_EXPORT%" (
    echo [LOI] Khong tim thay file export tai: %IDF_EXPORT%
    echo Vui long kiem tra lai duong dan esp-idf tren o C:
    pause
    exit /b 1
)

call "%IDF_EXPORT%"

:: 3. Chon cong COM: tham so dau tien > bien TARGET_PORT > mac dinh COM6
:: Vi du: kynangso-monitor-firmware.bat COM13
:: Tu dong tim cong: kynangso-monitor-firmware.bat AUTO
if not "%~1"=="" set "TARGET_PORT=%~1"
if not defined TARGET_PORT set "TARGET_PORT=COM6"

if /I "%TARGET_PORT%"=="AUTO" (
    echo [INFO] Che do: Tu dong nhan dien cong COM [Auto-detect]...
    set "PORT_PARAM="
) else (
    echo [INFO] Che do: Co dinh tren cong %TARGET_PORT%
    set "PORT_PARAM=-p %TARGET_PORT%"
)

:: 4. Flash Firmware va mo Serial Monitor
echo.
echo ========================================================
echo Dang mo Serial Monitor tren ESP32-S3...
echo [*] Meo: Nhan [Ctrl + ]] de thoat khoi man hinh Monitor
echo ========================================================
echo.

idf.py %PORT_PARAM% monitor

:: 5. Kiem tra ma loi tra ve
if %ERRORLEVEL% neq 0 (
    echo.
    echo ========================================================
    echo [THAT BAI] Qua trinh nap firmware hoac mo port bi loi!
    echo ========================================================
    pause
)
