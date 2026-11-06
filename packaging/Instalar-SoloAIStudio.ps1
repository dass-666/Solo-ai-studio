# Instala Solo AI Studio (VST3) en la carpeta estándar de plugins VST3 de Windows y comprueba la copia.
#
# Uso (PowerShell como administrador, desde la carpeta del paquete):
#   powershell -ExecutionPolicy Bypass -File .\Instalar-SoloAIStudio.ps1
#   powershell -ExecutionPolicy Bypass -File .\Instalar-SoloAIStudio.ps1 -Desinstalar
#
# Qué hace: copia la carpeta "Solo AI Studio.vst3" que está junto a este script a
# C:\Program Files\Common Files\VST3\ y verifica que el binario copiado es idéntico (SHA-256).
# No toca nada más: ni FL Studio, ni el registro, ni otros plugins.
param([switch]$Desinstalar)
$ErrorActionPreference = "Stop"

$nombre  = "Solo AI Studio.vst3"
$destino = Join-Path $env:CommonProgramFiles "VST3"          # C:\Program Files\Common Files\VST3
$origen  = Join-Path $PSScriptRoot $nombre
$binRel  = "Contents\x86_64-win\Solo AI Studio.vst3"         # el binario dentro del paquete VST3

$esAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $esAdmin) {
    Write-Host "Hace falta PowerShell como administrador (clic derecho > Ejecutar como administrador)." -ForegroundColor Red
    exit 1
}

$instalado = Join-Path $destino $nombre
if ($Desinstalar) {
    if (Test-Path $instalado) { Remove-Item -Recurse -Force $instalado; Write-Host "Desinstalado: $instalado" }
    else { Write-Host "No estaba instalado en $instalado" }
    exit 0
}

if (-not (Test-Path (Join-Path $origen $binRel))) {
    Write-Host "No encuentro '$nombre' junto a este script ($origen)." -ForegroundColor Red
    exit 1
}

New-Item -ItemType Directory -Force $destino | Out-Null
if (Test-Path $instalado) {
    Write-Host "Sustituyendo la versión instalada anterior..."
    Remove-Item -Recurse -Force $instalado   # solo la carpeta de este plugin
}
Copy-Item -Recurse $origen $destino

$h1 = (Get-FileHash (Join-Path $origen $binRel) -Algorithm SHA256).Hash
$h2 = (Get-FileHash (Join-Path $instalado $binRel) -Algorithm SHA256).Hash
if ($h1 -ne $h2) {
    Write-Host "La copia no coincide con el original (SHA-256 distinto)." -ForegroundColor Red
    exit 1
}
Write-Host "Instalado y verificado: $instalado" -ForegroundColor Green
Write-Host "SHA-256 del binario: $h2"
Write-Host "Ahora en FL Studio: Options > Manage plugins > Find installed plugins."
exit 0
