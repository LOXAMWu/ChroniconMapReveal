<#
    Build ChroniconMapReveal.dll

    Requirements: MinGW-w64 (gcc/g++ with C++20 support).
    Usage:  powershell -ExecutionPolicy Bypass -File build.ps1
            powershell -ExecutionPolicy Bypass -File build.ps1 -GccDir "C:\msys64\mingw64\bin"
#>
[CmdletBinding()]
param(
    [string]$OutDir,
    [string]$GccDir
)

$ErrorActionPreference = 'Stop'

if (-not $OutDir) { $OutDir = Join-Path $PSScriptRoot 'build' }

function Find-Toolchain {
    param([string]$Preferred)

    $candidates = @()
    if ($Preferred) { $candidates += $Preferred }

    foreach ($name in @('g++', 'gcc')) {
        $cmd = Get-Command $name -ErrorAction SilentlyContinue
        if ($cmd) { $candidates += (Split-Path -Parent $cmd.Source) }
    }

    $candidates += @(
        'C:\Programs\mingw64\bin',
        'C:\mingw64\bin',
        'C:\msys64\mingw64\bin',
        'C:\Program Files\mingw64\bin',
        (Join-Path $env:LOCALAPPDATA 'Programs\mingw64\bin')
    )

    foreach ($dir in $candidates) {
        if (-not $dir) { continue }
        $gpp = Join-Path $dir 'g++.exe'
        $gcc = Join-Path $dir 'gcc.exe'
        if ((Test-Path -LiteralPath $gpp) -and (Test-Path -LiteralPath $gcc)) {
            return [pscustomobject]@{ Gxx = $gpp; Gcc = $gcc }
        }
    }

    return $null
}

Write-Host ''
Write-Host '=== Building ChroniconMapReveal ===' -ForegroundColor Cyan

$toolchain = Find-Toolchain -Preferred $GccDir
if (-not $toolchain) {
    Write-Host 'MinGW-w64 (gcc/g++) not found.' -ForegroundColor Red
    Write-Host 'Install MSYS2 (https://www.msys2.org) and "pacman -S mingw-w64-x86_64-gcc",'
    Write-Host 'or point -GccDir at a folder containing gcc.exe / g++.exe.'
    exit 1
}

Write-Host "Using: $($toolchain.Gxx)"
& $toolchain.Gxx --version | Select-Object -First 1

$includeAurie = Join-Path $PSScriptRoot 'third_party\aurie'
$includeMinHook = Join-Path $PSScriptRoot 'third_party\minhook\include'
$minhookSrc = Join-Path $PSScriptRoot 'third_party\minhook\src'
$sourceDir = Join-Path $PSScriptRoot 'src'
$sources = @(Get-ChildItem -LiteralPath $sourceDir -Filter '*.cpp' | Sort-Object Name | ForEach-Object { $_.Name })

$objDir = Join-Path $OutDir 'obj'
New-Item -ItemType Directory -Force -Path $objDir | Out-Null

$cSources = @('hook.c', 'buffer.c', 'trampoline.c')
$objects = @()

foreach ($name in $cSources) {
    $obj = Join-Path $objDir ($name -replace '\.c$', '.o')
    Write-Host "  compiling $name"
    & $toolchain.Gcc -c -O2 -I $includeMinHook -I $minhookSrc (Join-Path $minhookSrc $name) -o $obj
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $objects += $obj
}

$hde = Join-Path $minhookSrc 'hde\hde64.c'
$hdeObj = Join-Path $objDir 'hde64.o'
Write-Host '  compiling hde64.c'
& $toolchain.Gcc -c -O2 -I $includeMinHook -I $minhookSrc $hde -o $hdeObj
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$objects += $hdeObj

$cxxFlags = @(
    '-std=c++20', '-O2', '-DNDEBUG', '-DWIN32_LEAN_AND_MEAN',
    '-finput-charset=UTF-8', '-fexec-charset=UTF-8', '-fwide-exec-charset=UTF-16LE'
)

foreach ($name in $sources) {
    $obj = Join-Path $objDir ($name -replace '\.cpp$', '.o')
    Write-Host "  compiling $name"
    & $toolchain.Gxx @cxxFlags -c -I $includeAurie -I $includeMinHook -I $sourceDir (Join-Path $sourceDir $name) -o $obj
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $objects += $obj
}

$dll = Join-Path $OutDir 'ChroniconMapReveal.dll'
Write-Host '  linking ChroniconMapReveal.dll'
& $toolchain.Gxx `
    @cxxFlags -shared `
    -I $includeAurie -I $includeMinHook `
    @objects `
    -o $dll `
    -static -static-libgcc -static-libstdc++ `
    -luser32 -lgdi32 -Wno-unknown-pragmas -Wno-ignored-attributes

if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ''
Write-Host "Built: $dll" -ForegroundColor Green
Write-Host "SHA256: $((Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash)"
