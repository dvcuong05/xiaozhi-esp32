- Build firmware `python scripts\build.py mrkenddo-robot --language vi-VN`
- Install by output file build folder `E:\Workspaces\Robotic\xiaozhi-esp32\build\merge-bin` read `.\tools\esp32-devtool\README.md`

**Notes**
- build: `python scripts/build.py bread-compact-esp32 --name bread-compact-esp32 --language vi-VN 2>&1`
- flash:
  $env:PYTHONUTF8 = "1"
  $env:IDF_TOOLS_PATH = "C:\Espressif"
  . "C:\esp\v6.1\esp-idf\export.ps1" 2>&1 | Select-Object -Last 2
  Write-Host "`n=== Flashing to COM13 ===" -ForegroundColor Yellow
  idf.py -p COM13 flash 2>&1
- monitor
- reset
- debug
