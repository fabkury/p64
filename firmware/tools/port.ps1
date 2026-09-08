# Prints the board's COM port, or a note when it is not connected.
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\env.ps1" | Out-Null
$port = Get-P64Port
if ($port) { Write-Output $port } else { Write-Output 'Board not found (looking for USB VID 303A / PID 1001 on the USB port).' }
