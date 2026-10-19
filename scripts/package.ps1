# Gera a release completa em dist\:
#   Aeonis-Setup-<versão>.exe        instalador (não pede administrador; atualiza a versão C# no lugar)
#   Aeonis-<versão>-portable.zip     a mesma pasta, sem instalar
#   Aeonis-Portable-<versão>.exe     arquivo único (extrai para %TEMP% e abre)
# Nada para instalar por fora: Qt, FFmpeg (linkado no exe) e o runtime do VC++ vão juntos.
#   .\scripts\package.ps1             # compila (com testes) e empacota
#   .\scripts\package.ps1 -SkipBuild
param(
    [switch]$SkipBuild,
    [switch]$NoPortable,
    [string]$QtDir = 'C:\Qt\6.11.3\msvc2022_64'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$release = Join-Path $root 'build\Release'
$dist = Join-Path $root 'dist'
$stage = Join-Path $dist 'Aeonis'
$version = (Select-String -Path (Join-Path $root 'CMakeLists.txt') -Pattern 'project\(Aeonis VERSION ([\d.]+)').Matches[0].Groups[1].Value

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1')
    if ($LASTEXITCODE) { Write-Error 'build/testes falharam'; exit 1 }
}

$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
$crt = Get-ChildItem (Join-Path $vs 'VC\Redist\MSVC') -Directory | Sort-Object Name -Descending |
    ForEach-Object { Get-ChildItem (Join-Path $_.FullName 'x64') -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue } |
    Select-Object -First 1
$iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe") |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { Write-Error 'Inno Setup 6 não encontrado (winget install JRSoftware.InnoSetup)'; exit 1 }
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'

# 1) Executável + Qt (só os módulos usados)
if (Test-Path $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item (Join-Path $release 'Aeonis.exe') $stage
# (via cmd: os avisos do windeployqt no stderr não podem derrubar o script no PowerShell 5.1)
$deploy = Join-Path $QtDir 'bin\windeployqt.exe'
$deployArgs = "--release --no-translations --no-system-d3d-compiler --no-opengl-sw --no-compiler-runtime " +
    "--skip-plugin-types qmltooling,generic,networkinformation --qmldir `"$(Join-Path $root 'qml')`" `"$(Join-Path $stage 'Aeonis.exe')`""
cmd /c "`"$deploy`" $deployArgs >nul 2>&1"
if ($LASTEXITCODE) { throw 'windeployqt falhou' }

# Estilos do Qt Quick Controls que o Aeonis não usa (o FluentWinUI3 se apoia no Basic e no Fusion).
foreach ($style in 'Material', 'Imagine', 'Universal') {
    Remove-Item (Join-Path $stage "qml\QtQuick\Controls\$style") -Recurse -Force -ErrorAction SilentlyContinue
    Get-ChildItem $stage -Filter "Qt6QuickControls2$style*.dll" | Remove-Item -Force
}
# TLS: o Schannel do Windows basta (sem OpenSSL).
Remove-Item (Join-Path $stage 'tls\qopensslbackend.dll'), (Join-Path $stage 'tls\qcertonlybackend.dll') -Force -ErrorAction SilentlyContinue
# Backend de mídia: só o FFmpeg do Qt (o do Media Foundation não toca HEVC sem extensão).
Remove-Item (Join-Path $stage 'multimedia\windowsmediaplugin.dll') -Force -ErrorAction SilentlyContinue

# 2) Runtime do Visual C++ ao lado do exe (dispensa o vc_redist)
Copy-Item (Join-Path $crt.FullName '*.dll') $stage

# 3) Licenças: GPL do Aeonis + textos das dependências
Copy-Item (Join-Path $root 'LICENSE') (Join-Path $stage 'LICENSE.txt')
Copy-Item (Join-Path $root 'README.md') $stage
$licenses = Join-Path $stage 'licenses'
New-Item -ItemType Directory -Force $licenses | Out-Null
Get-ChildItem (Join-Path $root 'vcpkg_installed\x64-windows-static-md\share') -Directory |
    Where-Object { Test-Path (Join-Path $_.FullName 'copyright') } |
    ForEach-Object { Copy-Item (Join-Path $_.FullName 'copyright') (Join-Path $licenses "$($_.Name).txt") }
Copy-Item (Join-Path $QtDir '..\..\Licenses\LICENSE') (Join-Path $licenses 'qt.txt') -ErrorAction SilentlyContinue
@"
O Aeonis é software livre (GPL-3.0-or-later). Código-fonte desta versão:
https://github.com/JonasHenriqueDev/aeonis/tree/v$version

Inclui: Qt $((Split-Path (Split-Path $QtDir -Parent) -Leaf)) (LGPLv3), FFmpeg (GPL, com x264, NVENC, AMF e oneVPL) linkado no Aeonis.exe
e o runtime do Microsoft Visual C++. As licenças estão nesta pasta.
"@ | Set-Content (Join-Path $licenses 'README.txt') -Encoding utf8

# 4) Instalador
& $iscc "/DAppVersion=$version" "/DStageDir=$stage" (Join-Path $root 'installer\aeonis.iss') | Out-Null
if ($LASTEXITCODE) { throw 'Inno Setup falhou' }

if (-not $NoPortable) {
    # 5) ZIP portátil
    $zip = Join-Path $dist "Aeonis-$version-portable.zip"
    Remove-Item $zip -ErrorAction SilentlyContinue
    Compress-Archive -Path $stage -DestinationPath $zip -CompressionLevel Optimal

    # 6) Exe portátil de arquivo único: launcher nativo com o ZIP embutido
    $portableBuild = Join-Path $root 'build\portable'
    & $cmake -S (Join-Path $root 'portable') -B $portableBuild -A x64 "-DPAYLOAD_ZIP=$zip" | Out-Null
    if ($LASTEXITCODE) { throw 'Configuração do launcher portátil falhou' }
    & $cmake --build $portableBuild --config Release | Out-Null
    if ($LASTEXITCODE) { throw 'Build do launcher portátil falhou' }
    Copy-Item (Join-Path $portableBuild 'Release\AeonisPortable.exe') (Join-Path $dist "Aeonis-Portable-$version.exe") -Force
}

Get-ChildItem $dist -File | Where-Object { $_.Name -like "Aeonis-*$version*" } |
    ForEach-Object { "{0}  {1:N1} MB" -f $_.Name, ($_.Length / 1MB) }
"Pasta: {0:N1} MB" -f ((Get-ChildItem $stage -Recurse -File | Measure-Object Length -Sum).Sum / 1MB)
