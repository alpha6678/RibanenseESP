#Requires -Version 5.1
<#
.SYNOPSIS
  CLI do RibanenseESP (firmware ESP-IDF). Entrada: rbesp.cmd
#>
[CmdletBinding()]
param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $CliArgs
)

$ErrorActionPreference = 'Stop'
$ScriptRoot = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
$ProjectRoot = Split-Path -Parent $ScriptRoot
. (Join-Path $ScriptRoot 'esp-idf-env.ps1')
$null = Sync-ProjectIdentity -ProjectRoot $ProjectRoot -Quiet

function Show-Help {
    @"
RibanenseESP — CLI do firmware (placa E32R28T-1)

Uso:  rbesp <comando> [args]
      rbesp os <comando> [args]
      rbesp app <comando> <Slug> [args]

Comandos:
  help                         Esta ajuda
  doctor                       Confere IDF, porta USB, identidade, chave e URLs
  whoami                       Conta Git/GitHub deste projeto (alpha6678)
  version                      Mostra versoes do OS e dos apps
  list                         Lista OS e apps da placa
  ports                        Lista portas seriais (marca a CH340)
  build                        Compila o OS (espelho C:\fw)
  flash [COM] [--primeiro]     Compila e grava o OS (detecta CH340)
  monitor [COM]                Serial do IDF, sem recompilar
  app build <Slug>             Compila um app em firmware/apps
  app flash <Slug> [COM]       Grava o app no chip (substitui o OS)
  bump os|<Slug> [patch|minor|major]
  publish os|<Slug>|all [--dry-run] [-Yes]
  release os|<Slug> <semver>
  keygen                       Gera chave ECDSA P-256 em secrets/
  sign                         Assina firmware.json com o SHA atual
  verify                       Verifica a assinatura de firmware.json
  logs [ip]                    GET /log da placa na LAN
  clean                        Remove artifacts/
  install [user|session]       Shim rbesp/rb no PATH

Primeiro USB (placa nova ou recuperacao):
  rbesp ports
  rbesp flash --primeiro

Aliases: os=esp, build=compilar, flash=gravar, publish=empacotar, list=ls
Porta: argumento COMx, senao RIBANENSE_PORT, senao CH340 detectada.
Conta deste repo: version.json (githubOwner/gitEmail). A CLI troca o gh so aqui.
"@ | Write-Host
}

function Get-EspApps {
    $root = Join-Path $ProjectRoot 'firmware\apps'
    if (-not (Test-Path -LiteralPath $root)) { return @() }
    Get-ChildItem -LiteralPath $root -Directory | Where-Object {
        Test-Path -LiteralPath (Join-Path $_.FullName 'app.json')
    }
}

function Get-NextSemver {
    param([string] $Current, [ValidateSet('patch', 'minor', 'major')] [string] $Part = 'patch')
    if ($Current -notmatch '^(\d+)\.(\d+)\.(\d+)') {
        throw "Versao invalida: $Current"
    }
    $x = [int]$Matches[1]; $y = [int]$Matches[2]; $z = [int]$Matches[3]
    switch ($Part) {
        'major' { return "$($x + 1).0.0" }
        'minor' { return "$x.$($y + 1).0" }
        default { return "$x.$y.$($z + 1)" }
    }
}

function Get-LatestTagForPrefix {
    param([string] $Prefix)
    $tags = @(& git -C $ProjectRoot tag --list "$Prefix*")
    $best = $null
    $bestKey = $null
    foreach ($t in $tags) {
        if ($t -notmatch [regex]::Escape($Prefix) + '(\d+)\.(\d+)\.(\d+)$') { continue }
        $key = '{0:D6}.{1:D6}.{2:D6}' -f [int]$Matches[1], [int]$Matches[2], [int]$Matches[3]
        if (-not $bestKey -or $key -gt $bestKey) {
            $bestKey = $key
            $best = $t
        }
    }
    return $best
}

function Test-ChangedSinceTag {
    param([string] $Tag, [string[]] $Prefixes, [string[]] $Exclude = @())
    if (-not $Tag) { return $true }
    $files = @(& git -C $ProjectRoot diff --name-only "$Tag..HEAD")
    foreach ($f in $files) {
        $skip = $false
        foreach ($ex in $Exclude) {
            if ($f -replace '\\', '/' -like ($ex -replace '\\', '/')) { $skip = $true; break }
        }
        if ($skip) { continue }
        foreach ($p in $Prefixes) {
            $norm = $p -replace '\\', '/'
            if (($f -replace '\\', '/').StartsWith($norm)) { return $true }
        }
    }
    return $false
}

function Invoke-OsMirrorBuild {
    param([string[]] $IdfArgs = @('build'))
    $osMirror = Sync-OsMirror -ProjectRoot $ProjectRoot
    Invoke-IdfBuild -ProjectDir $osMirror -ExtraArgs $IdfArgs
}

function Show-SerialPorts {
    $ports = @(Get-RibanenseSerialPorts)
    if ($ports.Count -eq 0) {
        Write-Host "Nenhuma porta serial. Conecte o USB-C da E32R28T-1 (CH340)."
        return
    }
    foreach ($p in $ports) {
        $mark = if ($p.Board) { 'PLACA' } else { '    ' }
        $color = if ($p.Board) { 'Green' } else { 'DarkGray' }
        Write-Host ("{0}  {1,-6}  {2}" -f $mark, $p.Port, $p.Name) -ForegroundColor $color
    }
}

function Get-FlashOptions {
    param([string[]] $Items)
    $port = $null
    $erase = $false
    foreach ($a in @($Items)) {
        if ($a -match '^COM\d+$') { $port = $a; continue }
        if ($a -in @('--primeiro', '--first', '--erase', '-e')) { $erase = $true; continue }
    }
    [pscustomobject]@{ Port = $port; Erase = $erase }
}

function Invoke-OsFlash {
    param([string] $Port, [switch] $Erase)
    $port = Resolve-RibanensePort $Port
    if ($Erase) {
        Write-Host "Flash inicial em $port — apaga a flash e grava bootloader + particoes + OS." -ForegroundColor Cyan
        Write-Host "NVS e Wi-Fi da placa somem. OTA so funciona depois deste USB." -ForegroundColor Yellow
        Invoke-OsMirrorBuild -IdfArgs @('-p', $port, 'erase-flash', 'flash')
    } else {
        Write-Host "Gravando OS em $port (bootloader + particoes + app, sem apagar NVS)." -ForegroundColor Cyan
        Invoke-OsMirrorBuild -IdfArgs @('-p', $port, 'flash')
    }
}

function Invoke-OsMonitor {
    param([string] $Port)
    $port = Resolve-RibanensePort $Port
    $osMirror = Join-Path (Get-IdfMirrorRoot) 'ribanense-esp'
    if (-not (Test-Path -LiteralPath (Join-Path $osMirror 'build_idf.bat'))) {
        $osMirror = Sync-OsMirror -ProjectRoot $ProjectRoot
    }
    Write-Host "Monitor $port (sem rebuild). Ctrl+] para sair." -ForegroundColor Cyan
    Invoke-IdfBuild -ProjectDir $osMirror -ExtraArgs @('-p', $port, 'monitor')
}

function Invoke-AppMirrorBuild {
    param([Parameter(Mandatory)] [string] $Slug, [string[]] $IdfArgs = @('build'))
    $appDir = Join-Path $ProjectRoot "firmware\apps\$Slug"
    if (-not (Test-Path -LiteralPath (Join-Path $appDir 'app.json'))) {
        throw "App da placa nao encontrado: $appDir"
    }
    $mirror = Get-IdfMirrorRoot
    $appMirror = Join-Path $mirror "apps\$Slug"
    Write-Host "Espelhando $Slug para $appMirror ..." -ForegroundColor Cyan
    Invoke-RobocopyMirror -Source $appDir -Destination $appMirror
    Invoke-RobocopyMirror -Source (Join-Path $ProjectRoot 'firmware\esp-sdk') -Destination (Join-Path $mirror 'esp-sdk')
    Copy-OsVersionJsonToSdk -ProjectRoot $ProjectRoot -SdkDest (Join-Path $mirror 'esp-sdk')
    Invoke-IdfBuild -ProjectDir $appMirror -ExtraArgs $IdfArgs
}

function Invoke-Whoami {
    $id = Sync-ProjectIdentity -ProjectRoot $ProjectRoot
    $gitName = (& git -C $ProjectRoot config --local --get user.name)
    $gitEmail = (& git -C $ProjectRoot config --local --get user.email)
    $origin = (& git -C $ProjectRoot remote get-url origin)
    $ghPc = Get-GhActiveUser
    Write-Host "projeto   $($id.Owner)/$($id.Repo)"
    Write-Host "git       $gitName <$gitEmail>"
    Write-Host "origin    $origin"
    Write-Host "gh PC     $(if ($ghPc) { $ghPc } else { '(nao logado)' })"
    Write-Host "gh repo   $($id.Owner)  (rbesp publish/release e git push deste repo)"
}

function Invoke-Doctor {
    $script:ok = $true
    function Note([bool] $Good, [string] $Msg) {
        if ($Good) { Write-Host "[OK] $Msg" -ForegroundColor Green }
        else { Write-Host "[!!] $Msg" -ForegroundColor Yellow; $script:ok = $false }
    }
    $idf = Test-Path -LiteralPath 'C:\esp\esp-idf\tools\idf.py'
    Note $idf "ESP-IDF em C:\esp\esp-idf"
    $id = Sync-ProjectIdentity -ProjectRoot $ProjectRoot -Quiet
    $gh = [bool] (Get-Command gh -ErrorAction SilentlyContinue)
    Note $gh "GitHub CLI (gh)"
    if ($gh) {
        $hasOwner = $false
        $token = & gh auth token --user $id.Owner 2>$null
        if ($token) { $hasOwner = $true }
        Note $hasOwner "Conta gh $($id.Owner) logada (publish/release usam ela)"
        $login = Get-GhActiveUser
        if ($login -and $login -ne $id.Owner) {
            Write-Host "[..] gh ativo do PC: $login. Neste repo a CLI usa $($id.Owner)." -ForegroundColor Cyan
        } elseif ($login) {
            Write-Host "[OK] gh ativo: $login" -ForegroundColor Green
        }
    }
    $gitName = (& git -C $ProjectRoot config --local --get user.name)
    $gitEmail = (& git -C $ProjectRoot config --local --get user.email)
    Note ($gitName -eq $id.Name -and $gitEmail -eq $id.Email) "git local $($id.Name) <$($id.Email)>"
    $ports = @(Get-RibanenseSerialPorts | Where-Object { $_.Board })
    if ($ports.Count -eq 0) {
        Write-Host "[..] Nenhuma CH340. Conecte o USB-C para flash inicial." -ForegroundColor Yellow
    } else {
        foreach ($p in $ports) {
            Note $true "Placa USB $($p.Port) — $($p.Name)"
        }
    }
    $key = Get-SigningKeyPath -ProjectRoot $ProjectRoot
    Note (Test-Path -LiteralPath $key) "Chave de assinatura $key"
    $ossl = Get-OpenSslPath
    Note ([bool] $ossl) "openssl ($ossl)"
    $info = Get-OsVersionInfo -ProjectRoot $ProjectRoot
    Write-Host "[..] version.json $($info.version) $($info.githubOwner)/$($info.githubRepo)" -ForegroundColor Cyan
    $fw = Get-Content -LiteralPath (Join-Path $ProjectRoot 'firmware\ribanense-esp\firmware.json') -Raw
    $bad = $fw -match 'BananaSuisa|desenvolvimentoLocatelli'
    Note (-not $bad) "firmware.json aponta para $($info.githubOwner)/$($info.githubRepo)"
    $cat = Get-Content -LiteralPath (Join-Path $ProjectRoot 'catalog\esp-catalog.json') -Raw
    Note ($cat -notmatch 'BananaSuisa|desenvolvimentoLocatelli') "esp-catalog.json sem owner antigo"
    if ($script:ok) { Write-Host "`nDoctor OK." -ForegroundColor Green } else { throw "Doctor encontrou problemas." }
}

function Invoke-ShowVersions {
    $info = Get-OsVersionInfo -ProjectRoot $ProjectRoot
    Write-Host "OS RibanenseESP  $($info.version)  ($($info.githubOwner)/$($info.githubRepo))"
    foreach ($d in Get-EspApps) {
        $m = Get-Content -LiteralPath (Join-Path $d.FullName 'app.json') -Raw | ConvertFrom-Json
        Write-Host ("app {0,-12} {1}" -f $d.Name, $m.version)
    }
}

function Invoke-List {
    Write-Host "OS   RibanenseESP"
    foreach ($d in Get-EspApps) {
        Write-Host "app  $($d.Name)"
    }
}

function Set-EspAppVersion {
    param([string] $Slug, [string] $Version)
    $appJson = Join-Path $ProjectRoot "firmware\apps\$Slug\app.json"
    $m = Get-Content -LiteralPath $appJson -Raw
    $m = [regex]::Replace($m, '("version"\s*:\s*")[^"]+(")', { param($x) "$($x.Groups[1].Value)$Version$($x.Groups[2].Value)" }, 1)
    Set-Content -LiteralPath $appJson -Value $m -Encoding UTF8
    $cmake = Join-Path $ProjectRoot "firmware\apps\$Slug\CMakeLists.txt"
    if (Test-Path -LiteralPath $cmake) {
        $c = Get-Content -LiteralPath $cmake -Raw
        if ($c -match 'set\(PROJECT_VER') {
            $c = [regex]::Replace($c, '(set\(PROJECT_VER\s+")[^"]+("\))', { param($x) "$($x.Groups[1].Value)$Version$($x.Groups[2].Value)" }, 1)
            Set-Content -LiteralPath $cmake -Value $c -Encoding UTF8
        }
    }
}

function Invoke-Bump {
    param([string] $Target, [string] $Part = 'patch')
    if ($Part -notin @('patch', 'minor', 'major')) { $Part = 'patch' }
    if ($Target -in @('os', 'OS', 'RibanenseESP', 'esp')) {
        $cur = [string] (Get-OsVersionInfo -ProjectRoot $ProjectRoot).version
        $next = Get-NextSemver -Current $cur -Part $Part
        $null = Set-OsVersionInfo -ProjectRoot $ProjectRoot -Version $next
        Write-Host "OS $cur -> $next"
        return
    }
    $appDir = Join-Path $ProjectRoot "firmware\apps\$Target"
    if (-not (Test-Path -LiteralPath (Join-Path $appDir 'app.json'))) {
        throw "Alvo de bump desconhecido: $Target"
    }
    $m = Get-Content -LiteralPath (Join-Path $appDir 'app.json') -Raw | ConvertFrom-Json
    $next = Get-NextSemver -Current ([string] $m.version) -Part $Part
    Set-EspAppVersion -Slug $Target -Version $next
    Write-Host "$Target $($m.version) -> $next"
}

function Get-PublishPlan {
    $plan = @()
    $osTag = Get-LatestTagForPrefix -Prefix 'ribanense-esp-v'
    if (Test-ChangedSinceTag -Tag $osTag -Prefixes @('firmware/ribanense-esp/', 'firmware/esp-sdk/') -Exclude @('firmware/ribanense-esp/firmware.json', 'firmware/ribanense-esp/dist/*')) {
        $cur = if ($osTag -match '(\d+\.\d+\.\d+)$') { $Matches[1] } else { [string] (Get-OsVersionInfo -ProjectRoot $ProjectRoot).version }
        $plan += [pscustomobject]@{ Kind = 'os'; Name = 'OS'; Current = $cur; Next = (Get-NextSemver $cur 'patch'); Reason = $(if ($osTag) { "mudou desde $osTag" } else { 'sem tag anterior' }) }
    }
    foreach ($d in Get-EspApps) {
        $m = Get-Content -LiteralPath (Join-Path $d.FullName 'app.json') -Raw | ConvertFrom-Json
        $prefix = if ($m.githubTagPrefix) { [string] $m.githubTagPrefix } else { "esp-$($d.Name.ToLowerInvariant())-v" }
        $tag = Get-LatestTagForPrefix -Prefix $prefix
        $paths = @("firmware/apps/$($d.Name)/", 'firmware/esp-sdk/')
        if (Test-ChangedSinceTag -Tag $tag -Prefixes $paths -Exclude @('firmware/esp-sdk/components/board/include/ribanense_esp_version.h*')) {
            $cur = [string] $m.version
            $plan += [pscustomobject]@{ Kind = 'esp-app'; Name = $d.Name; Current = $cur; Next = (Get-NextSemver $cur 'patch'); Reason = $(if ($tag) { "mudou desde $tag" } else { 'sem tag anterior' }) }
        }
    }
    return $plan
}

function Invoke-PublishAll {
    param([switch] $DryRun, [switch] $Yes)
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw "git nao encontrado." }
    if (-not $DryRun -and -not (Get-Command gh -ErrorAction SilentlyContinue)) { throw "gh nao encontrado." }
    & git -C $ProjectRoot fetch --tags 2>$null | Out-Null
    $plan = @(Get-PublishPlan)
    if ($plan.Count -eq 0) {
        Write-Host "Nada para publicar."
        return
    }
    foreach ($i in $plan) {
        Write-Host ("{0,-12} {1} -> {2}  ({3})" -f $i.Name, $i.Current, $i.Next, $i.Reason)
    }
    if ($DryRun) { return }
    if (-not $Yes) {
        $ans = Read-Host "Publicar estes itens? (s/N)"
        if ($ans -notin @('s', 'S', 'y', 'Y', 'sim')) { throw "Cancelado." }
    }
    $verFiles = @()
    foreach ($i in $plan) {
        if ($i.Kind -eq 'os') {
            $verFiles += Set-OsVersionInfo -ProjectRoot $ProjectRoot -Version $i.Next
        } else {
            Set-EspAppVersion -Slug $i.Name -Version $i.Next
            $verFiles += (Join-Path $ProjectRoot "firmware\apps\$($i.Name)\app.json")
        }
    }
    Push-Location $ProjectRoot
    try {
        & git add -- $verFiles
        $staged = @(& git diff --cached --name-only | Where-Object { $_ })
        if ($staged.Count -gt 0) {
            & git commit -m "chore(release): bump de versoes (publish all)"
            & git push origin HEAD
        }
    } finally { Pop-Location }
    foreach ($i in $plan) {
        & (Join-Path $ScriptRoot 'release.ps1') -App $i.Name -Version $i.Next
    }
}

function Invoke-Keygen {
    $ossl = Get-OpenSslPath
    if (-not $ossl) { throw "openssl nao encontrado." }
    $dir = Join-Path $ProjectRoot 'secrets'
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $pem = Join-Path $dir 'ribanense-ota.pem'
    $pub = Join-Path $dir 'ribanense-ota.pub.pem'
    if (Test-Path -LiteralPath $pem) {
        throw "Ja existe $pem. Remova se quiser gerar outra (e atualize a pubkey no firmware)."
    }
    & $ossl ecparam -name prime256v1 -genkey -noout -out $pem
    if ($LASTEXITCODE -ne 0) { throw "falha ao gerar chave." }
    & $ossl ec -in $pem -pubout -out $pub
    $lines = Get-Content -LiteralPath $pub
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.AppendLine('#pragma once')
    [void]$sb.AppendLine('')
    [void]$sb.AppendLine('/* ECDSA P-256. Par da chave privada em secrets/ribanense-ota.pem (fora do git). */')
    [void]$sb.AppendLine('#define RIBANENSEESP_OTA_PUBKEY \')
    foreach ($line in $lines) {
        [void]$sb.AppendLine(('    "{0}\n" \' -f $line))
    }
    $txt = $sb.ToString().TrimEnd()
    if ($txt.EndsWith('\')) { $txt = $txt.TrimEnd('\').TrimEnd() }
    $txt += "`n"
    $out = Join-Path $ProjectRoot 'firmware\esp-sdk\components\board\include\ribanense_ota_pubkey.h'
    Set-Content -LiteralPath $out -Value $txt -Encoding UTF8
    Write-Host "Chave privada: $pem"
    Write-Host "Chave publica: $pub"
    Write-Host "Header:        $out"
}

function Invoke-SignManifest {
    $fwPath = Join-Path $ProjectRoot 'firmware\ribanense-esp\firmware.json'
    $fw = Get-Content -LiteralPath $fwPath -Raw | ConvertFrom-Json
    $canon = Get-OtaCanonical -Product $fw.product -Version $fw.version -Sha256 $fw.sha256
    $sig = Invoke-OtaSignCanonical -ProjectRoot $ProjectRoot -Canonical $canon
    $null = Set-FirmwareManifestPointer -ProjectRoot $ProjectRoot -Version $fw.version -Url $fw.url -Sha256 $fw.sha256
    Write-Host "Assinado $canon"
    Write-Host $sig
}

function Invoke-VerifyManifest {
    $fwPath = Join-Path $ProjectRoot 'firmware\ribanense-esp\firmware.json'
    $fw = Get-Content -LiteralPath $fwPath -Raw | ConvertFrom-Json
    $canon = Get-OtaCanonical -Product $fw.product -Version $fw.version -Sha256 $fw.sha256
    if (-not $fw.sig) { throw "firmware.json sem campo sig." }
    if (Invoke-OtaVerifyCanonical -ProjectRoot $ProjectRoot -Canonical $canon -SigHex ([string] $fw.sig)) {
        Write-Host "Assinatura OK ($canon)" -ForegroundColor Green
    } else {
        throw "Assinatura invalida."
    }
}

function Invoke-BoardLogs {
    param([string] $Ip)
    if (-not $Ip) {
        throw "Informe o IP da placa: rbesp logs 192.168.0.230"
    }
    $url = "http://$Ip/log"
    Write-Host $url -ForegroundColor Cyan
    Invoke-WebRequest -Uri $url -UseBasicParsing -TimeoutSec 10 | Select-Object -ExpandProperty Content
}

function Invoke-Clean {
    $art = Join-Path $ProjectRoot 'artifacts'
    if (Test-Path -LiteralPath $art) {
        Remove-Item -LiteralPath $art -Recurse -Force
        Write-Host "Removido $art"
    } else {
        Write-Host "Nada em artifacts/"
    }
}

function Invoke-Install {
    param([string] $Scope = 'user')
    & (Join-Path $ScriptRoot 'install-rb-command.ps1') -Scope $(if ($Scope -eq 'session') { 'Session' } else { 'User' })
}

$tokens = @($CliArgs | Where-Object { $_ -ne $null -and "$_" -ne '' })
if ($tokens.Count -eq 0) { Show-Help; exit 0 }

$t0 = $tokens[0].ToLowerInvariant()
$rest = @($tokens | Select-Object -Skip 1)
$osGroup = $false

# Grupo os/esp
if ($t0 -in @('os', 'esp', 'ribanenseesp')) {
    $osGroup = $true
    if ($rest.Count -eq 0) { Show-Help; exit 0 }
    $t0 = $rest[0].ToLowerInvariant()
    $rest = @($rest | Select-Object -Skip 1)
    if ($t0 -eq 'app') {
        if ($rest.Count -lt 2) { throw "Uso: rbesp os app publish|release|build|flash <Slug>" }
        $act = $rest[0].ToLowerInvariant()
        $slug = $rest[1]
        $more = @($rest | Select-Object -Skip 2)
        switch ($act) {
            { $_ -in @('build', 'compilar') } { Invoke-AppMirrorBuild -Slug $slug; break }
            { $_ -in @('flash', 'gravar') } {
                $opt = Get-FlashOptions $more
                Write-Host "Isto substitui o OS no chip pelo app $slug." -ForegroundColor Yellow
                Invoke-AppMirrorBuild -Slug $slug -IdfArgs @('-p', (Resolve-RibanensePort $opt.Port), 'flash')
                break
            }
            { $_ -in @('publish', 'empacotar', 'pack') } { & (Join-Path $ScriptRoot 'publish-esp-app.ps1') -App $slug @more; break }
            { $_ -in @('release', 'soltar') } {
                if (-not $more[0]) { throw "Uso: rbesp os app release <Slug> <semver>" }
                & (Join-Path $ScriptRoot 'release.ps1') -App $slug -Version $more[0]
            }
            default { throw "Acao de app desconhecida: $act" }
        }
        exit 0
    }
}

# Grupo app
if ($t0 -eq 'app') {
    if ($rest.Count -lt 2) { throw "Uso: rbesp app build|flash|publish|release <Slug>" }
    $act = $rest[0].ToLowerInvariant()
    $slug = $rest[1]
    $more = @($rest | Select-Object -Skip 2)
    switch ($act) {
        { $_ -in @('build', 'compilar') } { Invoke-AppMirrorBuild -Slug $slug; break }
        { $_ -in @('flash', 'gravar') } {
            $opt = Get-FlashOptions $more
            Write-Host "Isto substitui o OS no chip pelo app $slug." -ForegroundColor Yellow
            Invoke-AppMirrorBuild -Slug $slug -IdfArgs @('-p', (Resolve-RibanensePort $opt.Port), 'flash')
            break
        }
        { $_ -in @('publish', 'empacotar', 'pack') } { & (Join-Path $ScriptRoot 'publish-esp-app.ps1') -App $slug; break }
        { $_ -in @('release', 'soltar') } {
            if (-not $more[0]) { throw "Uso: rbesp app release <Slug> <semver>" }
            & (Join-Path $ScriptRoot 'release.ps1') -App $slug -Version $more[0]
        }
        default { throw "Acao de app desconhecida: $act" }
    }
    exit 0
}

switch ($t0) {
    { $_ -in @('help', '?', '-h', '--help') } { Show-Help }
    'doctor' { Invoke-Doctor }
    { $_ -in @('whoami', 'auth', 'user') } { Invoke-Whoami }
    { $_ -in @('version', 'versao') } { Invoke-ShowVersions }
    { $_ -in @('list', 'ls', 'apps') } { Invoke-List }
    { $_ -in @('build', 'compilar') } { Invoke-OsMirrorBuild }
    { $_ -in @('ports', 'portas', 'com') } { Show-SerialPorts }
    { $_ -in @('flash', 'gravar', 'primeiro') } {
        $opt = Get-FlashOptions $rest
        $erase = $opt.Erase -or ($t0 -eq 'primeiro')
        Invoke-OsFlash -Port $opt.Port -Erase:$erase
    }
    'monitor' {
        $opt = Get-FlashOptions $rest
        Invoke-OsMonitor -Port $opt.Port
    }
    'bump' {
        if (-not $rest[0]) { throw "Uso: rbesp bump os|<Slug> [patch|minor|major]" }
        $part = if ($rest[1]) { $rest[1] } else { 'patch' }
        Invoke-Bump -Target $rest[0] -Part $part
    }
    { $_ -in @('publish', 'empacotar', 'pack') } {
        $target = if ($osGroup -and (-not $rest[0] -or $rest[0].StartsWith('-'))) { 'os' } else { $rest[0] }
        $dry = $rest -contains '--dry-run' -or $rest -contains '--whatif' -or $rest -contains '-whatif'
        $yes = $rest -contains '-Yes' -or $rest -contains '--yes' -or $rest -contains '-y'
        if ($target -in @('all')) {
            Invoke-PublishAll -DryRun:$dry -Yes:$yes
        } elseif ($target -in @('os', 'OS', 'RibanenseESP', 'esp')) {
            & (Join-Path $ScriptRoot 'publish-os.ps1')
        } elseif ($target) {
            & (Join-Path $ScriptRoot 'publish-esp-app.ps1') -App $target
        } else {
            throw "Uso: rbesp publish os|<Slug>|all [--dry-run]"
        }
    }
    { $_ -in @('release', 'soltar') } {
        if ($osGroup -and $rest.Count -ge 1 -and $rest[0] -match '^\d+\.\d+') {
            & (Join-Path $ScriptRoot 'release.ps1') -App 'OS' -Version $rest[0]
        } elseif ($rest.Count -lt 2) {
            throw "Uso: rbesp release os|<Slug> <semver>"
        } else {
            & (Join-Path $ScriptRoot 'release.ps1') -App $rest[0] -Version $rest[1]
        }
    }
    'keygen' { Invoke-Keygen }
    'sign' { Invoke-SignManifest }
    'verify' { Invoke-VerifyManifest }
    { $_ -in @('logs', 'log') } { Invoke-BoardLogs -Ip $rest[0] }
    { $_ -in @('clean', 'limpar') } { Invoke-Clean }
    { $_ -in @('install', 'setup') } { Invoke-Install -Scope $rest[0] }
    default { throw "Comando desconhecido: $($tokens[0]). Use rbesp help." }
}
