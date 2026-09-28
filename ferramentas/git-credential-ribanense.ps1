#Requires -Version 5.1
# Helper de credencial Git deste clone.
# Usa o token da conta githubOwner se ela estiver logada.
# Senao, usa a conta gh ativa (colaborador com acesso de escrita).
param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $Rest
)

$ErrorActionPreference = 'Stop'
$op = if ($Rest -and $Rest.Count -gt 0) { $Rest[0].ToLowerInvariant() } else { 'get' }
if ($op -ne 'get') {
    exit 0
}

try {
    $null = [Console]::In.ReadToEnd()
} catch {
}

$projectRoot = Split-Path -Parent $PSScriptRoot
$infoPath = Join-Path $projectRoot 'firmware\ribanense-esp\version.json'
$owner = $null
if (Test-Path -LiteralPath $infoPath) {
    $info = Get-Content -LiteralPath $infoPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($info.githubOwner) { $owner = [string] $info.githubOwner }
}
if ([string]::IsNullOrWhiteSpace($owner)) {
    exit 1
}

$user = $owner
$token = & gh auth token --user $owner 2>$null
if (-not $token) {
    $token = & gh auth token 2>$null
    $user = (& gh api user --jq .login 2>$null)
}
if (-not $token -or [string]::IsNullOrWhiteSpace($user)) {
    exit 1
}
Write-Output "username=$user"
Write-Output "password=$token"
exit 0
