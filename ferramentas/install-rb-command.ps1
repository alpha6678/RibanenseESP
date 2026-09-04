#Requires -Version 5.1
<#
.SYNOPSIS
  Instala o comando global `rb` no Windows.

.DESCRIPTION
  Cria shims `rb.cmd` e `rb.ps1` em uma pasta de bin local e
  adiciona essa pasta ao PATH (escopo User ou Session).

.EXAMPLE
  .\ferramentas\install-rb-command.ps1

.EXAMPLE
  .\ferramentas\install-rb-command.ps1 -Scope Session
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
$rbCmdPath = Join-Path $projectRoot 'rb.cmd'

if (-not (Test-Path -LiteralPath $rbCmdPath)) {
    throw "rb.cmd nao encontrado em: $rbCmdPath"
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
    param(
        [string[]] $Entries,
        [string] $Target
    )
    $targetFull = [System.IO.Path]::GetFullPath($Target)
    foreach ($entry in $Entries) {
        if ($entry.Equals($targetFull, [System.StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }
    return $false
}

New-Item -ItemType Directory -Path $BinDir -Force | Out-Null

$cmdShimPath = Join-Path $BinDir 'rb.cmd'
$ps1ShimPath = Join-Path $BinDir 'rb.ps1'

$cmdShim = @(
    '@echo off'
    'setlocal'
    'powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0rb.ps1" %*'
    'endlocal'
    ''
) -join "`r`n"
Set-Content -LiteralPath $cmdShimPath -Value $cmdShim -Encoding Ascii

$escapedRbCmdPath = $rbCmdPath.Replace("'", "''")
$ps1Shim = @(
    "& '$escapedRbCmdPath' @args"
    ''
) -join "`r`n"
Set-Content -LiteralPath $ps1ShimPath -Value $ps1Shim -Encoding Unicode

Write-Host "[OK] Shim criado: $cmdShimPath" -ForegroundColor Green
Write-Host "[OK] Shim criado: $ps1ShimPath" -ForegroundColor Green

$currentPathEntries = Split-PathEntries -PathValue $env:PATH
if (-not (Test-PathEntryContains -Entries $currentPathEntries -Target $BinDir)) {
    $env:PATH = "$BinDir;$env:PATH"
}

if ($Scope -eq 'User') {
    $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    $userEntries = Split-PathEntries -PathValue $userPath
    if (-not (Test-PathEntryContains -Entries $userEntries -Target $BinDir)) {
        $newUserPath = if ([string]::IsNullOrWhiteSpace($userPath)) {
            $BinDir
        } else {
            "$userPath;$BinDir"
        }
        [Environment]::SetEnvironmentVariable('Path', $newUserPath, 'User')
        Write-Host "[OK] PATH de usuario atualizado com: $BinDir" -ForegroundColor Green
    } else {
        Write-Host "[..] PATH de usuario ja contem: $BinDir" -ForegroundColor Cyan
    }
    Write-Host ""
    Write-Host "Abra um novo terminal para usar: rb help" -ForegroundColor Yellow
} else {
    Write-Host ""
    Write-Host "Escopo Session nao persiste automaticamente no terminal pai quando chamado via rb.cmd." -ForegroundColor Cyan
    Write-Host "Para usar agora neste terminal, execute:" -ForegroundColor Yellow
    Write-Host "  `$env:PATH = ""$BinDir;`$env:PATH""" -ForegroundColor Yellow
}
