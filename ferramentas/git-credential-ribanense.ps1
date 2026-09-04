#Requires -Version 5.1
# Helper de credencial Git deste repo: sempre alpha6678 (version.json),
# independente da conta gh ativa no PC.
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
$owner = 'alpha6678'
if (Test-Path -LiteralPath $infoPath) {
    $info = Get-Content -LiteralPath $infoPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($info.githubOwner) { $owner = [string] $info.githubOwner }
}

$token = & gh auth token --user $owner 2>$null
if (-not $token) {
    exit 1
}
Write-Output "username=$owner"
Write-Output "password=$token"
exit 0
