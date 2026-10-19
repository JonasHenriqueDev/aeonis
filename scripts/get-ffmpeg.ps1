# Baixa o FFmpeg 8.1 (build GPL de BtbN, com ddagrab, NVENC, AMF, QSV e x264 — o mesmo da release)
# para %LOCALAPPDATA%\Aeonis\ffmpeg, onde o Aeonis procura automaticamente.
param(
    [string]$Url = "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-n8.1-latest-win64-gpl-8.1.zip",
    [string]$Destination = (Join-Path $env:LOCALAPPDATA "Aeonis\ffmpeg")
)
$ErrorActionPreference = "Stop"
$tmp = Join-Path ([IO.Path]::GetTempPath()) ("aeonis-ffmpeg-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force $tmp | Out-Null
try {
    $zip = Join-Path $tmp "ffmpeg.zip"
    Write-Host "Baixando $Url ..."
    Invoke-WebRequest -Uri $Url -OutFile $zip -UseBasicParsing
    Expand-Archive -Path $zip -DestinationPath $tmp
    $exe = Get-ChildItem $tmp -Recurse -Filter ffmpeg.exe | Select-Object -First 1
    if (-not $exe) { throw "ffmpeg.exe não encontrado no pacote." }
    New-Item -ItemType Directory -Force $Destination | Out-Null
    Copy-Item $exe.FullName (Join-Path $Destination "ffmpeg.exe") -Force
    $filters = & (Join-Path $Destination "ffmpeg.exe") -hide_banner -filters 2>&1 | Out-String
    if ($filters -notmatch "ddagrab") { Write-Warning "Este build não tem o filtro ddagrab; o Aeonis precisa dele (FFmpeg 6.1+)." }
    Write-Host "FFmpeg instalado em $Destination"
}
finally {
    Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
}
