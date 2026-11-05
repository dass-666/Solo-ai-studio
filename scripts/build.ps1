# Compilación reproducible en Windows (requiere Visual Studio 2022 con "Desarrollo para escritorio con C++", CMake y Git).
#   .\scripts\build.ps1            -> compila, ejecuta pruebas y el host de prueba
#   .\scripts\build.ps1 -Install   -> además copia el VST3 a C:\Program Files\Common Files\VST3 (requiere PowerShell como administrador)
param([switch]$Install)
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure

$vst3 = "build/plugin/SoloAIStudio_artefacts/Release/VST3/Solo AI Studio.vst3"
& "build/plugin/SoloHostTest_artefacts/Release/SoloHostTest.exe" $vst3
if ($LASTEXITCODE -ne 0) { throw "El host de prueba falló" }

if ($Install) {
    $dest = "C:\Program Files\Common Files\VST3"
    Copy-Item -Recurse -Force $vst3 $dest
    Write-Host "Instalado en $dest\Solo AI Studio.vst3"
}
Write-Host "OK: $vst3"
