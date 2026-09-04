#Requires -Version 5.1
<#
.SYNOPSIS
  Instala os comandos globais `rbesp` e `rb` no Windows.
#>
[CmdletBinding()]
param(
    [ValidateSet('User', 'Session')]
    [string] $Scope = 'User',

    [string] $BinDir = (Join-Path $env:LOCALAPPDATA 'RibanenseSolucoes\bin')
)

$ErrorActionPreference = 'Stop'

$scriptRoot = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
$projectRoot = Split-Path -Parent $scriptRoot
$entry = Join-Path $projectRoot 'rbesp.cmd'
if (-not (Test-Path -LiteralPath $entry)) {
    throw "rbesp.cmd nao encontrado em: $entry"
}

function Split-PathEntries {
    param([string] $PathValue)
    if ([string]::IsNullOrWhiteSpace($PathValue)) { return @() }
    return @(
        foreach ($item in ($PathValue -split ';')) {
            $trimmed = $item.Trim()
            if (-not [string]::IsNullOrWhiteSpace($trimmed)) {
                [System.IO.Path]::GetFullPath($trimmed)
            }
        }
    )
}

function Test-PathEntryContains {
    param([string[]] $Entries, [string] $Target)
    $targetFull = [System.IO.Path]::GetFullPath($Target)
    foreach ($e in $Entries) {
        if ($e.Equals($targetFull, [System.StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }
    return $false
}

New-Item -ItemType Directory -Path $BinDir -Force | Out-Null
$escaped = $entry.Replace("'", "''")

foreach ($name in @('rbesp', 'rb')) {
    $cmdShim = Join-Path $BinDir "$name.cmd"
    $ps1Shim = Join-Path $BinDir "$name.ps1"
    @(
        '@echo off'
        'setlocal'
        "call `"$entry`" %*"
        'endlocal'
        ''
    ) -join "`r`n" | Set-Content -LiteralPath $cmdShim -Encoding Ascii
    "& '$escaped' @args`r`n" | Set-Content -LiteralPath $ps1Shim -Encoding Unicode
    Write-Host "[OK] Shim: $cmdShim" -ForegroundColor Green
}

$current = Split-PathEntries -PathValue $env:PATH
if (-not (Test-PathEntryContains -Entries $current -Target $BinDir)) {
    $env:PATH = "$BinDir;$env:PATH"
}

if ($Scope -eq 'User') {
    $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    $userEntries = Split-PathEntries -PathValue $userPath
    if (-not (Test-PathEntryContains -Entries $userEntries -Target $BinDir)) {
        $newUserPath = if ([string]::IsNullOrWhiteSpace($userPath)) { $BinDir } else { "$userPath;$BinDir" }
        [Environment]::SetEnvironmentVariable('Path', $newUserPath, 'User')
        Write-Host "[OK] PATH de usuario atualizado com: $BinDir" -ForegroundColor Green
    } else {
        Write-Host "[..] PATH de usuario ja contem: $BinDir" -ForegroundColor Cyan
    }
    Write-Host ""
    Write-Host "Abra um novo terminal para usar: rbesp help" -ForegroundColor Yellow
} else {
    Write-Host "Escopo Session: `$env:PATH = `"$BinDir;`$env:PATH`"" -ForegroundColor Yellow
}
