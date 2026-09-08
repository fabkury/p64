# Builds (if needed) and flashes the board over its USB port.
#   .\tools\flash.ps1                 auto-detects the board's COM port
#   .\tools\flash.ps1 -Port COM7      explicit port
#   .\tools\flash.ps1 -Monitor        open the serial monitor afterwards
param(
    [string]$Port,
    [switch]$Monitor
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\env.ps1"
if (-not $Port) { $Port = Get-P64Port }
if (-not $Port) { throw 'Board not found: connect the USB port (not POWER) and try again, or pass -Port COMx.' }
Push-Location "$PSScriptRoot\.."
try {
    if ($Monitor) { idf.py -p $Port flash monitor } else { idf.py -p $Port flash }
} finally { Pop-Location }
