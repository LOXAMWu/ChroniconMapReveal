<#
    Install Chronicon Map Reveal.

    * downloads Aurie (AurieCore.dll + AuriePatcher.exe) if missing
    * backs up and patches <game>\Chronicon.exe with the Aurie loader
    * copies the built mod DLL into <game>\mods\aurie\

    Usage:
      powershell -ExecutionPolicy Bypass -File tools\install.ps1
      powershell -ExecutionPolicy Bypass -File tools\install.ps1 -GameDir "D:\Steam\steamapps\common\Chronicon" `
                                                                  -ModDll "..\build\ChroniconMapReveal.dll"
#>
[CmdletBinding()]
param(
    [string]$GameDir,
    [string]$ModDll,
    [string]$AurieVersion = 'v2.0.2',
    [switch]$SkipDownload
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

if (-not $ModDll) { $ModDll = Join-Path $RepoRoot 'build\ChroniconMapReveal.dll' }
if (-not (Test-Path -LiteralPath $ModDll)) {
    Write-Host "Mod DLL not found: $ModDll" -ForegroundColor Red
    Write-Host 'Build it first:  powershell -ExecutionPolicy Bypass -File build.ps1'
    exit 1
}

function Find-ChroniconDir {
    foreach ($reg in @('HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam')) {
        try { $steamPath = (Get-ItemProperty -Path $reg -ErrorAction Stop).SteamPath } catch { continue }
        if (-not $steamPath) { continue }
        $libs = @($steamPath)
        $vdf = Join-Path $steamPath 'steamapps\libraryfolders.vdf'
        if (Test-Path -LiteralPath $vdf) {
            foreach ($m in [regex]::Matches((Get-Content -LiteralPath $vdf -Raw), '"path"\s*"([^"]+)"')) {
                $libs += ($m.Groups[1].Value -replace '\\\\', '\')
            }
        }
        foreach ($lib in $libs) {
            $candidate = Join-Path $lib 'steamapps\common\Chronicon'
            if (Test-Path -LiteralPath (Join-Path $candidate 'Chronicon.exe')) { return $candidate }
        }
    }

    foreach ($drive in @('C', 'D', 'E', 'F')) {
        foreach ($sub in @(
            '{0}:\Program Files (x86)\Steam\steamapps\common\Chronicon',
            '{0}:\Program Files\Steam\steamapps\common\Chronicon',
            '{0}:\Steam\steamapps\common\Chronicon',
            '{0}:\SteamLibrary\steamapps\common\Chronicon'
        )) {
            $candidate = $sub -f $drive
            if (Test-Path -LiteralPath (Join-Path $candidate 'Chronicon.exe')) { return $candidate }
        }
    }
    return $null
}

Write-Host ''
Write-Host '=== Chronicon Map Reveal - install ===' -ForegroundColor Cyan

if (-not $GameDir) { $GameDir = Find-ChroniconDir }
if (-not $GameDir) {
    Write-Host 'Chronicon installation not found, pass -GameDir explicitly.' -ForegroundColor Red
    exit 1
}

$exe = Join-Path $GameDir 'Chronicon.exe'
if (Get-Process -Name 'Chronicon' -ErrorAction SilentlyContinue) {
    Write-Host 'Chronicon is running - close it first.' -ForegroundColor Red
    exit 1
}

$AurieCore = Join-Path $GameDir 'AurieCore.dll'
$AuriePatcher = Join-Path $RepoRoot 'tools\AuriePatcher.exe'

if (-not $SkipDownload) {
    if (-not (Test-Path -LiteralPath $AurieCore)) {
        Write-Host 'downloading AurieCore.dll ...'
        Invoke-WebRequest -Uri "https://github.com/AurieFramework/Aurie/releases/download/$AurieVersion/AurieCore.dll" `
            -OutFile $AurieCore -UseBasicParsing
    }
    if (-not (Test-Path -LiteralPath $AuriePatcher)) {
        Write-Host 'downloading AuriePatcher.exe ...'
        Invoke-WebRequest -Uri "https://github.com/AurieFramework/Aurie/releases/download/$AurieVersion/AuriePatcher.exe" `
            -OutFile $AuriePatcher -UseBasicParsing
    }
}

if (-not (Test-Path -LiteralPath $AuriePatcher)) {
    Write-Host 'AuriePatcher.exe missing (no network?).' -ForegroundColor Red
    exit 1
}

$Backup = "$exe.mapreveal.bak"
if (-not (Test-Path -LiteralPath $Backup)) {
    Copy-Item -LiteralPath $exe -Destination $Backup -Force
    Write-Host "backed up original exe -> $Backup" -ForegroundColor DarkGray
}

Write-Host 'patching Chronicon.exe with the Aurie loader ...'
& $AuriePatcher $exe $AurieCore install
if ($LASTEXITCODE -ne 0) {
    Write-Host 'AuriePatcher failed.' -ForegroundColor Red
    exit 1
}

$ModsDir = Join-Path $GameDir 'mods\aurie'
New-Item -ItemType Directory -Force -Path $ModsDir | Out-Null
Copy-Item -LiteralPath $ModDll -Destination (Join-Path $ModsDir 'ChroniconMapReveal.dll') -Force

Write-Host ''
Write-Host 'Installed. The map will now stay fully revealed.' -ForegroundColor Green
Write-Host "  mod      : $ModsDir\ChroniconMapReveal.dll"
Write-Host "  log      : $GameDir\aurie.log"
Write-Host '  panel    : on/off state of both mechanisms, drawn in the top-right corner of the game window'
Write-Host '  hotkeys  : numpad 1 = mechanism A / numpad 2 = mechanism B / numpad 3 = print status / numpad 0 = panel'
