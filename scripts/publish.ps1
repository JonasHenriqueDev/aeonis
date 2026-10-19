# Gera dist\Aeonis.exe (arquivo único, ~1 MB, usa o .NET 8 Desktop Runtime instalado).
# -SelfContained gera um exe maior (~70 MB) que não depende do runtime.
param([switch]$SelfContained)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$dist = Join-Path $root "dist"
$sc = if ($SelfContained) { "true" } else { "false" }
dotnet publish (Join-Path $root "src\Aeonis\Aeonis.csproj") -c Release -r win-x64 --self-contained $sc `
    -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -o $dist
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$ff = Join-Path $env:LOCALAPPDATA "Aeonis\ffmpeg\ffmpeg.exe"
if (Test-Path $ff) { Copy-Item $ff (Join-Path $dist "ffmpeg.exe") -Force }
Write-Host "Pronto: $dist"
