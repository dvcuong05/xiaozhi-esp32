[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^COM\d+$')]
    [string]$Port,

    [ValidateRange(9600, 921600)]
    [int]$Baud = 115200,

    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$env:PYTHONUTF8 = '1'
$env:IDF_TOOLS_PATH = 'C:\Espressif'

$idfRoot = 'C:\esp\v6.1\esp-idf'
$idfExport = Join-Path $idfRoot 'export.ps1'
if (-not (Test-Path -LiteralPath $idfExport)) {
    throw "Không tìm thấy ESP-IDF export.ps1 tại '$idfExport'."
}

$availablePorts = [System.IO.Ports.SerialPort]::GetPortNames()
if ($Port -notin $availablePorts) {
    $portList = if ($availablePorts.Count -gt 0) { $availablePorts -join ', ' } else { 'không có cổng COM nào' }
    throw "Không thấy $Port. Cổng hiện có: $portList. Hãy chọn cổng USB của D1 Mini."
}

Push-Location $PSScriptRoot
try {
    . $idfExport

    $buildDir = Join-Path $PSScriptRoot 'build-v3'
    if (-not $SkipBuild) {
        Write-Host 'Đang build firmware ESP32 D1 Mini...'
        & idf.py -B build-v3 -DIDF_TARGET=esp32 build
        if ($LASTEXITCODE -ne 0) {
            throw "Build thất bại với mã lỗi $LASTEXITCODE."
        }
    }

    $flasherConfigPath = Join-Path $buildDir 'flasher_args.json'
    if (-not (Test-Path -LiteralPath $flasherConfigPath)) {
        throw "Không tìm thấy '$flasherConfigPath'. Hãy bỏ -SkipBuild để build trước."
    }

    $flasherConfig = Get-Content -LiteralPath $flasherConfigPath -Raw | ConvertFrom-Json
    if ($flasherConfig.extra_esptool_args.chip -ne 'esp32') {
        throw "Build hiện tại không phải target ESP32 cổ điển; dừng để tránh nạp nhầm chip."
    }

    $appOffset = $flasherConfig.app.offset
    $appPath = Join-Path $buildDir $flasherConfig.app.file
    if (-not (Test-Path -LiteralPath $appPath)) {
        throw "Không tìm thấy firmware app '$appPath'."
    }

    Write-Host "Đang nạp app D1 Mini lên $Port tại offset $appOffset (baud $Baud)."
    Write-Host 'Chỉ ghi app; giữ nguyên bootloader, partition table và NVS.'

    $esptoolArgs = @(
        '--chip', 'esp32',
        '--port', $Port,
        '--baud', "$Baud",
        '--before', 'default-reset',
        '--after', 'hard-reset',
        'write-flash'
    )
    $esptoolArgs += @($flasherConfig.write_flash_args)
    $esptoolArgs += @($appOffset, $appPath)

    & python -m esptool @esptoolArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Nạp firmware thất bại với mã lỗi $LASTEXITCODE."
    }

    Write-Host 'Nạp xong. Esptool cần báo Hash of data verified để xác nhận dữ liệu đã ghi.'
} finally {
    Pop-Location
}
