# Activates ESP-IDF v5.5.4 (the EIM install under C:\esp) for the current shell,
# including the ESP32-S3 toolchain, and defines Get-P64Port.
#
#   . .\tools\env.ps1        (dot-source it; the other scripts here do that for you)
#
# Paths come from the EIM installation on this machine; adjust them if ESP-IDF
# lives somewhere else. See firmware/README.md.

$ErrorActionPreference = 'Stop'

$env:IDF_PATH = 'C:\esp\v5.5.4\esp-idf'
$env:IDF_TOOLS_PATH = 'C:\Espressif\tools'
$env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\tools\python\v5.5.4\venv'
$env:IDF_CCACHE_ENABLE = '1'

if (-not (Test-Path "$env:IDF_PATH\export.ps1")) {
    throw "ESP-IDF not found at $env:IDF_PATH. Install it (see firmware/README.md) or edit tools/env.ps1."
}

# export.ps1 runs whatever `python` comes first on PATH; make sure that is the IDF venv.
$env:PATH = "$env:IDF_PYTHON_ENV_PATH\Scripts;$env:PATH"
. "$env:IDF_PATH\export.ps1"

# The board's USB port is the ESP32-S3's native USB Serial/JTAG (VID 303A, PID 1001).
# Returns its COM port name, or $null when the board is not connected.
function global:Get-P64Port {
    if ($env:P64_PORT) { return $env:P64_PORT }
    $dev = Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
        Where-Object { $_.PNPDeviceID -like 'USB\VID_303A&PID_1001*' -and $_.Name -match '\((COM\d+)\)' } |
        Select-Object -First 1
    if ($dev -and $dev.Name -match '\((COM\d+)\)') { return $Matches[1] }
    return $null
}
