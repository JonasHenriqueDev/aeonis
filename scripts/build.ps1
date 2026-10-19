# Configura e compila o Aeonis (C++20, Qt 6, FFmpeg do vcpkg) e roda os testes.
#   .\scripts\build.ps1                 # Release + testes
#   .\scripts\build.ps1 -Config Debug
#   .\scripts\build.ps1 -SkipTests
param(
    [string]$Config = 'Release',
    [switch]$SkipTests,
    [string]$QtDir = 'C:\Qt\6.11.3\msvc2022_64',
    [string]$Vcpkg = 'D:\Dev\vcpkg'
)
# Avisos do CMake vão para stderr; não trate como erro (PowerShell 5.1).
$ErrorActionPreference = 'Continue'
$root = Split-Path $PSScriptRoot -Parent

$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
$generator = if ((& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property catalog_productLineVersion) -eq '2022') {
    'Visual Studio 17 2022' } else { 'Visual Studio 18 2026' }

# FFmpeg (avcodec/avformat com NVENC, AMF, QSV e x264) do manifesto vcpkg.json; usa o cache binário do vcpkg.
& "$Vcpkg\vcpkg.exe" install --triplet x64-windows-static-md "--x-manifest-root=$root" "--x-install-root=$root\vcpkg_installed"
if ($LASTEXITCODE) { exit $LASTEXITCODE }

& $cmake -S $root -B "$root\build" -G $generator -A x64 `
    "-DCMAKE_PREFIX_PATH=$QtDir" `
    "-DCMAKE_TOOLCHAIN_FILE=$Vcpkg\scripts\buildsystems\vcpkg.cmake" `
    '-DVCPKG_TARGET_TRIPLET=x64-windows-static-md' `
    "-DVCPKG_INSTALLED_DIR=$root\vcpkg_installed" `
    '-DVCPKG_MANIFEST_INSTALL=OFF'
if ($LASTEXITCODE) { exit $LASTEXITCODE }

& $cmake --build "$root\build" --config $Config --parallel
if ($LASTEXITCODE) { exit $LASTEXITCODE }

if ($SkipTests) { exit 0 }
$env:PATH = "$QtDir\bin;$env:PATH"
& $ctest --test-dir "$root\build" -C $Config --output-on-failure
exit $LASTEXITCODE
