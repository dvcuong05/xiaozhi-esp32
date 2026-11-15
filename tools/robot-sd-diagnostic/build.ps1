param([switch]$Run, [string]$Port = 'COM13', [int]$Seconds = 35)
$ErrorActionPreference = 'Stop'
$env:IDF_TOOLS_PATH = 'C:\Espressif'
$env:PYTHONUTF8 = '1'
$idfRoot = 'C:\esp\v6.1\esp-idf'
$python = Join-Path $env:IDF_TOOLS_PATH 'python_env\idf6.1_py3.11_env\Scripts\python.exe'
$buildCommand = 'call "{0}\export.bat" && python "{0}\tools\idf.py" -C "{1}" build' -f $idfRoot, $PSScriptRoot
& cmd.exe /d /c $buildCommand
if ($LASTEXITCODE -ne 0) { throw 'Build SD diagnostic failed.' }
if ($Run) {
    & $python (Join-Path $PSScriptRoot 'run.py') --port $Port --seconds $Seconds
    if ($LASTEXITCODE -ne 0) { throw 'SD RAM diagnostic failed.' }
}
