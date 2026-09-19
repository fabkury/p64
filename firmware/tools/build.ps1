# Builds the firmware. Extra arguments are passed to idf.py (e.g. -v).
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\env.ps1"
Push-Location "$PSScriptRoot\.."
try { idf.py build @args } finally { Pop-Location }
