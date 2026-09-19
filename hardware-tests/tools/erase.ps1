# Erases the whole flash (bootloader, partitions, app, NVS). Auto-detects the port unless -Port is given.
param([string]$Port)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\env.ps1"
if (-not $Port) { $Port = Get-P64Port }
if (-not $Port) { throw 'Board not found: connect the USB port (not POWER) and try again, or pass -Port COMx.' }
Push-Location "$PSScriptRoot\.."
try { idf.py -p $Port erase-flash } finally { Pop-Location }
