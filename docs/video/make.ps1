# make.ps1: regenerates the p64b concept video from scratch (see README.md).
#   .\make.ps1            full quality, about two hours on an RTX 5050 laptop
#   .\make.ps1 -Quick     16 samples, every fourth frame: a 7.5 fps preview in a few minutes
param([switch]$Quick)
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
$openscad = "C:\Program Files\OpenSCAD\openscad.com"
$blender  = "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe"
New-Item -ItemType Directory -Force build | Out-Null

$pieces = "frame","board","mask","hub75","pwr","chip_pcb","chip_usb","chip_module","chip_headers","chip_socket",
          "ad_body","ad_plug","insert","enc_board","enc_sockets","enc_body","enc_metal","enc_nut","enc_knob","screws"
& $openscad -o build\values.echo -D 'piece="values"' pieces.scad 2>$null
foreach ($p in $pieces) { & $openscad -o "build\$p.stl" -D "piece=`"$p`"" pieces.scad 2>$null }

python bake_leds.py

$step = if ($Quick) { 4 } else { 1 }
$samples = if ($Quick) { 16 } else { 48 }
Remove-Item build\frames\*.png -ErrorAction SilentlyContinue
& $blender -b -P scene.py -- --step $step --samples $samples

$out = if ($Quick) { "..\p64b-concept-preview.mp4" } else { "..\p64b-concept.mp4" }
python compose.py --step $step --out $out
