#Requires -Version 5.1
<#
.SYNOPSIS
  Compila um app nativo da placa e gera zip (store) + SHA256 + app.json.

.PARAMETER App
  Nome da pasta em firmware/apps (ex.: Sobre).

.PARAMETER Version
  SemVer. Se omitida, le app.json.

.PARAMETER OutputDir
  Pasta de saida (default: artifacts\publish\Esp<App>).
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $App,
    [string] $Version,
    [string] $OutputDir
)

$ErrorActionPreference = 'Stop'
$ScriptRoot = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
$ProjectRoot = Split-Path -Parent $ScriptRoot
. (Join-Path $ScriptRoot 'esp-idf-env.ps1')

$appDir = Join-Path $ProjectRoot "firmware\apps\$App"
$manifestPath = Join-Path $appDir 'app.json'
$sdkSrc = Join-Path $ProjectRoot 'firmware\esp-sdk'
if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "App da placa nao encontrado: $appDir (falta app.json)."
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if (-not $Version) {
    $Version = [string] $manifest.version
}
if (-not $Version) { $Version = '0.1.0' }

$slug = if ($manifest.id -match '([^.]+)$') { $Matches[1] } else { $App.ToLowerInvariant() }
if (-not $OutputDir) {
    $OutputDir = Join-Path $ProjectRoot "artifacts\publish\Esp$App"
}
if (Test-Path -LiteralPath $OutputDir) {
    Remove-Item -LiteralPath $OutputDir -Recurse -Force
}
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

$mirror = Get-IdfMirrorRoot
$appMirror = Join-Path $mirror "apps\$App"
Write-Host "Espelhando $App para $appMirror ..." -ForegroundColor Cyan
Invoke-RobocopyMirror -Source $appDir -Destination $appMirror
Invoke-RobocopyMirror -Source $sdkSrc -Destination (Join-Path $mirror 'esp-sdk')
Copy-OsVersionJsonToSdk -ProjectRoot $ProjectRoot -SdkDest (Join-Path $mirror 'esp-sdk')

Write-Host "Compilando app da placa $App $Version ..." -ForegroundColor Cyan
Invoke-IdfBuild -ProjectDir $appMirror -ExtraArgs @('build')

$candidates = @(
    (Join-Path $appMirror "build\esp_$($slug).bin"),
    (Join-Path $appMirror 'build\esp_sobre.bin'),
    (Get-ChildItem -LiteralPath (Join-Path $appMirror 'build') -Filter '*.bin' -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -notmatch 'bootloader|partition|ota_data' } |
        Select-Object -First 1 -ExpandProperty FullName)
)
$built = $null
foreach ($c in $candidates) {
    if ($c -and (Test-Path -LiteralPath $c)) { $built = $c; break }
}
if (-not $built) {
    throw "Binario do app nao gerado em $appMirror\build."
}

$appBin = Join-Path $OutputDir 'app.bin'
Copy-Item -LiteralPath $built -Destination $appBin -Force
$null = Assert-BuiltVersion -BinPath $appBin -Version $Version
Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $OutputDir 'app.json') -Force

$zipName = "esp-$slug-$Version.zip"
$zipPath = Join-Path $OutputDir $zipName
New-StoredZip -ZipPath $zipPath -Entries @{
    'app.bin'  = $appBin
    'app.json' = (Join-Path $OutputDir 'app.json')
}
$hash = Write-Sha256Sidecar -FilePath $zipPath

Write-Host ""
Write-Host "Pacote do app da placa criado em: $OutputDir" -ForegroundColor Green
Write-Host "  Zip    : $zipName"
Write-Host "  SHA256 : $hash"
