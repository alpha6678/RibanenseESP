#Requires -Version 5.1
<#
.SYNOPSIS
  Compila o OS RibanenseESP e gera .bin + SHA256.

.PARAMETER Version
  SemVer no nome do asset. Se omitida, le RIBANENSEESP_VERSION.

.PARAMETER OutputDir
  Pasta de saida (default: artifacts\publish\RibanenseESP).
#>
[CmdletBinding()]
param(
    [string] $Version,
    [string] $OutputDir
)

$ErrorActionPreference = 'Stop'
$ScriptRoot = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
$ProjectRoot = Split-Path -Parent $ScriptRoot
. (Join-Path $ScriptRoot 'esp-idf-env.ps1')

$osSrc = Join-Path $ProjectRoot 'firmware\ribanense-esp'
$sdkSrc = Join-Path $ProjectRoot 'firmware\esp-sdk'
if (-not (Test-Path -LiteralPath $osSrc)) {
    throw "OS nao encontrado: $osSrc"
}
if (-not $Version) {
    $Version = [string] (Get-OsVersionInfo -ProjectRoot $ProjectRoot).version
    if (-not $Version) { throw "Nao foi possivel ler version.json." }
}

if (-not $OutputDir) {
    $OutputDir = Join-Path $ProjectRoot 'artifacts\publish\RibanenseESP'
}
if (Test-Path -LiteralPath $OutputDir) {
    Remove-Item -LiteralPath $OutputDir -Recurse -Force
}
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

$mirror = Get-IdfMirrorRoot
Write-Host "Espelhando OS para $mirror ..." -ForegroundColor Cyan
Invoke-RobocopyMirror -Source $osSrc -Destination (Join-Path $mirror 'ribanense-esp')
Invoke-RobocopyMirror -Source $sdkSrc -Destination (Join-Path $mirror 'esp-sdk')
Copy-OsVersionJsonToSdk -ProjectRoot $ProjectRoot -SdkDest (Join-Path $mirror 'esp-sdk')

$osMirror = Join-Path $mirror 'ribanense-esp'
Sync-IdfSdkconfigFromDefaults -ProjectDir $osMirror
Write-Host "Compilando RibanenseESP $Version ..." -ForegroundColor Cyan
Invoke-IdfBuild -ProjectDir $osMirror -ExtraArgs @('build')

$built = Join-Path $osMirror 'build\ribanense_esp.bin'
if (-not (Test-Path -LiteralPath $built)) {
    throw "Binario nao gerado: $built"
}

$binName = "ribanense-esp-$Version.bin"
$binPath = Join-Path $OutputDir $binName
Copy-Item -LiteralPath $built -Destination $binPath -Force
$hash = Write-Sha256Sidecar -FilePath $binPath

$fwJson = Join-Path $osSrc 'firmware.json'
if (Test-Path -LiteralPath $fwJson) {
    Copy-Item -LiteralPath $fwJson -Destination (Join-Path $OutputDir 'firmware.json') -Force
}

Write-Host ""
Write-Host "Pacote do OS criado em: $OutputDir" -ForegroundColor Green
Write-Host "  Bin    : $binName"
Write-Host "  SHA256 : $hash"
