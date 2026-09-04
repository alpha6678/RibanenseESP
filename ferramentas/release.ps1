#Requires -Version 5.1
<#
.SYNOPSIS
  Cria tag Git e publica GitHub Release (OS ou app da placa).
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $App,
    [Parameter(Mandatory = $true)] [string] $Version,
    [string] $TagPrefix
)

$ErrorActionPreference = 'Stop'
$ScriptRoot = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
$ProjectRoot = Split-Path -Parent $ScriptRoot
. (Join-Path $ScriptRoot 'esp-idf-env.ps1')

if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
    throw "GitHub CLI (gh) nao encontrado no PATH. Instale em https://cli.github.com/."
}
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "git nao encontrado no PATH."
}

function Get-ReleaseKind {
    param([string] $Name)
    if ($Name -in @('OS', 'RibanenseESP', 'Esp')) { return 'os' }
    $espManifest = Join-Path $ProjectRoot "firmware\apps\$Name\app.json"
    if (Test-Path -LiteralPath $espManifest) { return 'esp-app' }
    throw "Alvo desconhecido: $Name (use OS ou o slug de um app em firmware/apps)."
}

function Invoke-PointerCommit {
    param(
        [string[]] $Files = @(),
        [string[]] $TreePaths = @(),
        [string] $Message
    )
    Push-Location $ProjectRoot
    try {
        foreach ($f in $Files) {
            if (-not (Test-Path -LiteralPath $f)) { throw "Arquivo ausente para commit: $f" }
            & git add -- $f
            if ($LASTEXITCODE -ne 0) { throw "git add falhou: $f" }
        }
        foreach ($p in $TreePaths) {
            if (-not (Test-Path -LiteralPath $p)) { throw "Caminho ausente para commit: $p" }
            & git add -A -- $p
            if ($LASTEXITCODE -ne 0) { throw "git add -A falhou: $p" }
        }
        $staged = @(& git diff --cached --name-only | Where-Object { $_ })
        if ($staged.Count -eq 0) {
            Write-Host "Nada novo para commitar em ponteiro de release." -ForegroundColor Yellow
            return
        }
        & git commit -m $Message
        if ($LASTEXITCODE -ne 0) { throw "git commit do ponteiro falhou." }
        & git push origin HEAD
        if ($LASTEXITCODE -ne 0) { throw "git push do ponteiro falhou." }
    }
    finally {
        Pop-Location
    }
}

if ($Version -match '^\d+\.\d+$') {
    $Version = "$Version.0"
}

$kind = Get-ReleaseKind -Name $App
if (-not $TagPrefix) {
    $TagPrefix = switch ($kind) {
        'os' { 'ribanense-esp-v' }
        'esp-app' {
            $m = Get-Content -LiteralPath (Join-Path $ProjectRoot "firmware\apps\$App\app.json") -Raw | ConvertFrom-Json
            if ($m.githubTagPrefix) { [string] $m.githubTagPrefix } else { "esp-$($App.ToLowerInvariant())-v" }
        }
    }
}
$tag = "$TagPrefix$Version"

Push-Location $ProjectRoot
try {
    $existing = & git tag --list $tag
    if ($existing) {
        throw "Tag '$tag' ja existe. Remova (git tag -d $tag) ou use outra versao."
    }

    $outName = if ($kind -eq 'os') { 'RibanenseESP' } else { "Esp$App" }

    if ($kind -eq 'os') {
        & "$ScriptRoot\publish-os.ps1" -Version $Version
    } else {
        & "$ScriptRoot\publish-esp-app.ps1" -App $App -Version $Version
    }
    if ($LASTEXITCODE -ne 0) { throw "publish falhou para $App." }

    $outDir = Join-Path $ProjectRoot "artifacts\publish\$outName"
    $gh = Get-GithubOwnerRepo -ProjectRoot $ProjectRoot

    $assetBaseName = if ($kind -eq 'os') {
        "ribanense-esp-$Version.bin"
    } else {
        $m = Get-Content -LiteralPath (Join-Path $ProjectRoot "firmware\apps\$App\app.json") -Raw | ConvertFrom-Json
        $slug = if ($m.id -match '([^.]+)$') { $Matches[1] } else { $App.ToLowerInvariant() }
        "esp-$slug-$Version.zip"
    }
    $assetPath = Join-Path $outDir $assetBaseName
    $shaPath = "$assetPath.sha256"
    $manifestPath = Join-Path $outDir 'app.json'

    if (-not (Test-Path -LiteralPath $assetPath)) { throw "Asset nao encontrado: $assetPath" }
    if (-not (Test-Path -LiteralPath $shaPath)) { throw "SHA256 nao encontrado: $shaPath" }

    Write-Host "Criando tag $tag..." -ForegroundColor Cyan
    & git tag $tag
    if ($LASTEXITCODE -ne 0) { throw "git tag falhou." }
    Write-Host "Enviando tag para origin..." -ForegroundColor Cyan
    & git push origin $tag
    if ($LASTEXITCODE -ne 0) { throw "git push origin $tag falhou; release abortado." }

    $releaseTitle = if ($kind -eq 'os') { "RibanenseESP $Version" } else { "RibanenseESP $App $Version" }
    $releaseNotes = if ($kind -eq 'os') {
        "Release automatizado do OS RibanenseESP $Version (binario assinado ECDSA P-256)."
    } else {
        "Release automatizado do app da placa $App $Version."
    }

    Write-Host "Publicando release $tag..." -ForegroundColor Cyan
    $ghArgs = @('release', 'create', $tag, $assetPath, $shaPath, '--title', $releaseTitle, '--notes', $releaseNotes)
    if ($kind -eq 'esp-app' -and (Test-Path -LiteralPath $manifestPath)) {
        $ghArgs += $manifestPath
    }
    & gh @ghArgs
    if ($LASTEXITCODE -ne 0) { throw "gh release create falhou." }

    $hash = ((Get-Content -LiteralPath $shaPath -Raw).Trim() -split '\s+')[0]
    if ($kind -eq 'os') {
        $fw = Join-Path $ProjectRoot 'firmware\ribanense-esp\firmware.json'
        $distDir = Join-Path $ProjectRoot 'firmware\ribanense-esp\dist'
        New-Item -ItemType Directory -Force -Path $distDir | Out-Null
        Get-ChildItem -LiteralPath $distDir -Filter 'ribanense-esp-*.bin' -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -ne $assetBaseName } |
            Remove-Item -Force
        $distBin = Join-Path $distDir $assetBaseName
        Copy-Item -LiteralPath $assetPath -Destination $distBin -Force
        $url = "https://raw.githubusercontent.com/$($gh.Owner)/$($gh.Repo)/main/firmware/ribanense-esp/dist/$assetBaseName"
        $null = Set-FirmwareManifestPointer -ProjectRoot $ProjectRoot -Version $Version -Url $url -Sha256 $hash
        Invoke-PointerCommit -Files @($fw) -TreePaths @($distDir) -Message "chore(release): firmware.json $Version"
    }
    elseif ($kind -eq 'esp-app') {
        $cat = Join-Path $ProjectRoot 'catalog\esp-catalog.json'
        $m = Get-Content -LiteralPath (Join-Path $ProjectRoot "firmware\apps\$App\app.json") -Raw | ConvertFrom-Json
        $url = "https://github.com/$($gh.Owner)/$($gh.Repo)/releases/download/$tag/$assetBaseName"
        $raw = Get-Content -LiteralPath $cat -Raw
        $id = [regex]::Escape([string] $m.id)
        if ($raw -notmatch $id) {
            throw "App '$($m.id)' nao esta em catalog/esp-catalog.json."
        }
        $doc = $raw | ConvertFrom-Json
        $hit = $false
        foreach ($entry in $doc.apps) {
            if ($entry.id -eq $m.id) {
                $entry.version = $Version
                $entry.url = $url
                $entry.sha256 = $hash
                $entry.githubOwner = $gh.Owner
                $entry.githubRepo = $gh.Repo
                $hit = $true
            }
        }
        if (-not $hit) { throw "Entrada do catalogo nao encontrada para $($m.id)." }
        $doc | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $cat -Encoding UTF8
        Invoke-PointerCommit -Files @($cat) -Message "chore(release): esp-catalog $($m.id) $Version"
    }

    Write-Host ""
    Write-Host "Release $tag publicado." -ForegroundColor Green
}
finally {
    Pop-Location
}
