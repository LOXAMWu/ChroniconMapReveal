<#
    Uninstall Chronicon Map Reveal: removes the mod DLL and reverts the exe patch.

    Usage: powershell -ExecutionPolicy Bypass -File tools\uninstall.ps1 [-GameDir <path>]
#>
[CmdletBinding()]
param(
    [string]$GameDir
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

function Find-ChroniconDir {
    foreach ($reg in @('HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam')) {
        try { $steamPath = (Get-ItemProperty -Path $reg -ErrorAction Stop).SteamPath } catch { continue }
        if (-not $steamPath) { continue }
        $candidate = Join-Path $steamPath 'steamapps\common\Chronicon'
        if (Test-Path -LiteralPath (Join-Path $candidate 'Chronicon.exe')) { return $candidate }
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
Write-Host '=== Chronicon Map Reveal - uninstall ===' -ForegroundColor Cyan

if (-not $GameDir) { $GameDir = Find-ChroniconDir }
if (-not $GameDir) {
    Write-Host 'Chronicon installation not found, pass -GameDir explicitly.' -ForegroundColor Red
    exit 1
}

if (Get-Process -Name 'Chronicon' -ErrorAction SilentlyContinue) {
    Write-Host 'Chronicon is running - close it first.' -ForegroundColor Red
    exit 1
}

$exe = Join-Path $GameDir 'Chronicon.exe'
$Backup = "$exe.mapreveal.bak"
$ModDll = Join-Path $GameDir 'mods\aurie\ChroniconMapReveal.dll'
$AuriePatcher = Join-Path $RepoRoot 'tools\AuriePatcher.exe'
$AurieCore = Join-Path $GameDir 'AurieCore.dll'

if (Test-Path -LiteralPath $ModDll) {
    Remove-Item -LiteralPath $ModDll -Force
    Write-Host 'removed mod DLL' -ForegroundColor DarkGray
}

if ((Test-Path -LiteralPath $AuriePatcher) -and (Test-Path -LiteralPath $AurieCore)) {
    & $AuriePatcher $exe $AurieCore remove
    Write-Host 'reverted the Aurie exe patch' -ForegroundColor DarkGray
}

if (Test-Path -LiteralPath $Backup) {
    Copy-Item -LiteralPath $Backup -Destination $exe -Force
    Write-Host 'restored the original Chronicon.exe from backup' -ForegroundColor Green
}

Write-Host 'Uninstalled.' -ForegroundColor Green
