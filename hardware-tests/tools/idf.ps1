# Runs idf.py inside the firmware project with the ESP-IDF environment active.
#   .\tools\idf.ps1 menuconfig
#   .\tools\idf.ps1 size-components
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\env.ps1"
Push-Location "$PSScriptRoot\.."
try { idf.py @args } finally { Pop-Location }
