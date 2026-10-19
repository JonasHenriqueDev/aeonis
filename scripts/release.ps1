# Gera a release completa em dist\:
#   Aeonis-Setup-<versão>.exe   instalador (app + runtime .NET + FFmpeg), não pede admin
#   Aeonis-<versão>-portable.zip  mesma coisa, sem instalar
# Uso: scripts\release.ps1 [-FfmpegExe caminho\ffmpeg.exe] [-FfmpegLicense caminho\LICENSE.txt]
param(
    [string]$FfmpegExe = (Join-Path $env:LOCALAPPDATA "Aeonis\ffmpeg\ffmpeg.exe"),
    [string]$FfmpegLicense = ""
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$dist = Join-Path $root "dist"
$stage = Join-Path $dist "stage"
$csproj = Join-Path $root "src\Aeonis\Aeonis.csproj"
$version = ([xml](Get-Content $csproj)).Project.PropertyGroup.Version | Where-Object { $_ } | Select-Object -First 1

if (-not (Test-Path $FfmpegExe)) { throw "FFmpeg não encontrado em $FfmpegExe (rode scripts\get-ffmpeg.ps1 ou passe -FfmpegExe)." }
$iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe") |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw "Inno Setup 6 não encontrado (winget install JRSoftware.InnoSetup)." }

Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
# Self-contained em pasta (sem single-file comprimido): abre rápido e usa menos RAM;
# o instalador comprime tudo com LZMA2.
dotnet publish $csproj -c Release -r win-x64 --self-contained true -p:PublishSingleFile=false -o $stage
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Get-ChildItem $stage -Filter *.pdb | Remove-Item

Copy-Item $FfmpegExe (Join-Path $stage "ffmpeg.exe")
Copy-Item (Join-Path $root "LICENSE") (Join-Path $stage "LICENSE.txt")
Copy-Item (Join-Path $root "README.md") (Join-Path $stage "README.md")
if ($FfmpegLicense -and (Test-Path $FfmpegLicense)) { Copy-Item $FfmpegLicense (Join-Path $stage "FFMPEG-LICENSE.txt") }
@"
O ffmpeg.exe incluído é um programa separado, distribuído sob a GPL v3.
Build: https://github.com/BtbN/FFmpeg-Builds  |  Código-fonte: https://github.com/FFmpeg/FFmpeg
"@ | Set-Content (Join-Path $stage "FFMPEG-NOTICE.txt") -Encoding utf8

& $iscc "/DAppVersion=$version" "/DStageDir=$stage" (Join-Path $root "installer\aeonis.iss")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$zip = Join-Path $dist "Aeonis-$version-portable.zip"
Remove-Item $zip -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip -CompressionLevel Optimal

Get-ChildItem $dist -File | Where-Object { $_.Name -like "Aeonis-*" } |
    ForEach-Object { "{0}  {1:N1} MB" -f $_.Name, ($_.Length / 1MB) }
