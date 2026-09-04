#Requires -Version 5.1
# Funcoes compartilhadas para espelhar e compilar projetos IDF (caminho sem acento).

function Get-IdfMirrorRoot {
    if (-not [string]::IsNullOrWhiteSpace($env:RIBANENSE_IDF_MIRROR)) {
        return $env:RIBANENSE_IDF_MIRROR.TrimEnd('\', '/')
    }
    return 'C:\fw'
}

function Invoke-RobocopyMirror {
    param(
        [Parameter(Mandatory)] [string] $Source,
        [Parameter(Mandatory)] [string] $Destination,
        [string[]] $ExcludeDirs = @('build', 'managed_components', '.git')
    )
    if (-not (Test-Path -LiteralPath $Source)) {
        throw "Origem IDF nao encontrada: $Source"
    }
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    $xd = @()
    foreach ($d in $ExcludeDirs) { $xd += @('/XD', $d) }
    $args = @($Source, $Destination, '/E', '/PURGE', '/NFL', '/NDL', '/NJH', '/NJS', '/nc', '/ns', '/np') + $xd + @('/XF', 'sdkconfig')
    & robocopy @args | Out-Null
    if ($LASTEXITCODE -ge 8) {
        throw "robocopy falhou ($LASTEXITCODE) de $Source para $Destination"
    }
}

function Sync-IdfSdkconfigFromDefaults {
    param([Parameter(Mandatory)] [string] $ProjectDir)
    $defaults = Join-Path $ProjectDir 'sdkconfig.defaults'
    if (-not (Test-Path -LiteralPath $defaults)) {
        return
    }
    $parent = Split-Path -Parent $ProjectDir
    $leaf = Split-Path -Leaf $ProjectDir
    $stamp = Join-Path $parent "$leaf.sdkconfig.defaults.sha256"
    $hash = (Get-FileHash -LiteralPath $defaults -Algorithm SHA256).Hash.ToLowerInvariant()
    $prev = ''
    if (Test-Path -LiteralPath $stamp) {
        $prev = (Get-Content -LiteralPath $stamp -Raw).Trim()
    }
    if ($hash -eq $prev) {
        return
    }
    $sdk = Join-Path $ProjectDir 'sdkconfig'
    if (Test-Path -LiteralPath $sdk) {
        Remove-Item -LiteralPath $sdk -Force
        Write-Host "[!!] sdkconfig.defaults mudou; sdkconfig do espelho sera regenerado." -ForegroundColor Yellow
    }
    Set-Content -LiteralPath $stamp -Value $hash -Encoding ASCII
}

function Invoke-IdfBuild {
    param(
        [Parameter(Mandatory)] [string] $ProjectDir,
        [string[]] $ExtraArgs = @('build')
    )
    $bat = Join-Path $ProjectDir 'build_idf.bat'
    if (-not (Test-Path -LiteralPath $bat)) {
        throw "build_idf.bat nao encontrado: $bat"
    }
    if (-not (Test-Path -LiteralPath 'C:\esp\esp-idf\tools\idf.py') -and
        ([string]::IsNullOrWhiteSpace($env:IDF_PATH) -or
         -not (Test-Path -LiteralPath (Join-Path $env:IDF_PATH 'tools\idf.py')))) {
        throw "ESP-IDF nao encontrado. Instale em C:\esp\esp-idf ou defina IDF_PATH."
    }
    Push-Location $ProjectDir
    try {
        & cmd.exe /c "`"$bat`" $($ExtraArgs -join ' ')"
        if ($LASTEXITCODE -ne 0) {
            throw "idf.py $($ExtraArgs -join ' ') falhou em $ProjectDir (codigo $LASTEXITCODE)."
        }
    }
    finally {
        Pop-Location
    }
}

function Get-RibanenseSerialPorts {
    $list = @()
    $devs = @()
    try {
        $devs = @(Get-PnpDevice -Class Ports -Status OK -ErrorAction Stop)
    } catch {
        return @()
    }
    foreach ($d in $devs) {
        if ($d.FriendlyName -notmatch '\((COM\d+)\)') {
            continue
        }
        $port = $Matches[1]
        $id = [string] $d.InstanceId
        $name = [string] $d.FriendlyName
        $isBoard = ($name -match 'CH340|CP210|USB-SERIAL|USB Serial') -or
            ($id -match 'VID_1A86|VID_10C4|VID_303A')
        $list += [pscustomobject]@{
            Port    = $port
            Name    = $name
            Board   = [bool] $isBoard
            Usb     = ($id -match '^USB\\')
        }
    }
    return $list
}

function Resolve-RibanensePort {
    param([string] $Requested)
    if ($Requested -match '^COM\d+$') {
        return $Requested.ToUpperInvariant()
    }
    if ($env:RIBANENSE_PORT -match '^COM\d+$') {
        return $env:RIBANENSE_PORT.ToUpperInvariant()
    }
    $ports = @(Get-RibanenseSerialPorts)
    $board = @($ports | Where-Object { $_.Board })
    if ($board.Count -eq 1) {
        return $board[0].Port
    }
    if ($board.Count -gt 1) {
        $names = ($board | ForEach-Object { "$($_.Port) ($($_.Name))" }) -join ', '
        throw "Varias placas USB: $names. Passe a porta: rbesp flash COM8"
    }
    $extra = ($ports | ForEach-Object { $_.Port }) -join ', '
    if ($extra) {
        throw "Nenhuma CH340/USB-SERIAL. Conecte o USB-C da E32R28T-1. Outras portas: $extra"
    }
    throw "Nenhuma porta serial. Conecte o USB-C da E32R28T-1 (CH340)."
}

function Sync-OsMirror {
    param([Parameter(Mandatory)] [string] $ProjectRoot)
    $mirror = Get-IdfMirrorRoot
    $osSrc = Join-Path $ProjectRoot 'firmware\ribanense-esp'
    $sdkSrc = Join-Path $ProjectRoot 'firmware\esp-sdk'
    Write-Host "Espelhando OS para $mirror ..." -ForegroundColor Cyan
    Invoke-RobocopyMirror -Source $osSrc -Destination (Join-Path $mirror 'ribanense-esp')
    Invoke-RobocopyMirror -Source $sdkSrc -Destination (Join-Path $mirror 'esp-sdk')
    Copy-OsVersionJsonToSdk -ProjectRoot $ProjectRoot -SdkDest (Join-Path $mirror 'esp-sdk')
    $osMirror = Join-Path $mirror 'ribanense-esp'
    Sync-IdfSdkconfigFromDefaults -ProjectDir $osMirror
    return $osMirror
}

function Get-OsVersionPath {
    param([Parameter(Mandatory)] [string] $ProjectRoot)
    return (Join-Path $ProjectRoot 'firmware\ribanense-esp\version.json')
}

function Get-OsVersionInfo {
    param([Parameter(Mandatory)] [string] $ProjectRoot)
    $path = Get-OsVersionPath -ProjectRoot $ProjectRoot
    if (-not (Test-Path -LiteralPath $path)) {
        throw "version.json nao encontrado: $path"
    }
    return Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json
}

function Set-OsVersionInfo {
    param(
        [Parameter(Mandatory)] [string] $ProjectRoot,
        [Parameter(Mandatory)] [string] $Version
    )
    $path = Get-OsVersionPath -ProjectRoot $ProjectRoot
    $info = Get-OsVersionInfo -ProjectRoot $ProjectRoot
    $info.version = $Version
    $info | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $path -Encoding UTF8
    $fw = Join-Path $ProjectRoot 'firmware\ribanense-esp\firmware.json'
    if (Test-Path -LiteralPath $fw) {
        $raw = Get-Content -LiteralPath $fw -Raw -Encoding UTF8
        $raw = [regex]::Replace($raw, '("version"\s*:\s*")[^"]+(")', { param($m) "$($m.Groups[1].Value)$Version$($m.Groups[2].Value)" }, 1)
        Set-Content -LiteralPath $fw -Value $raw -Encoding UTF8
    }
    return $path
}

function Copy-OsVersionJsonToSdk {
    param(
        [Parameter(Mandatory)] [string] $ProjectRoot,
        [Parameter(Mandatory)] [string] $SdkDest
    )
    $src = Get-OsVersionPath -ProjectRoot $ProjectRoot
    if (-not (Test-Path -LiteralPath $src)) {
        return
    }
    New-Item -ItemType Directory -Path $SdkDest -Force | Out-Null
    Copy-Item -LiteralPath $src -Destination (Join-Path $SdkDest 'version.json') -Force
}

function Get-OpenSslPath {
    $candidates = @(
        'C:\Program Files\Git\usr\bin\openssl.exe',
        'C:\Program Files\Git\mingw64\bin\openssl.exe'
    )
    foreach ($c in $candidates) {
        if (Test-Path -LiteralPath $c) { return $c }
    }
    $cmd = Get-Command openssl -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

function Get-SigningKeyPath {
    param([Parameter(Mandatory)] [string] $ProjectRoot)
    if (-not [string]::IsNullOrWhiteSpace($env:RIBANENSE_SIGNING_KEY)) {
        return $env:RIBANENSE_SIGNING_KEY.Trim()
    }
    return (Join-Path $ProjectRoot 'secrets\ribanense-ota.pem')
}

function Get-OtaCanonical {
    param([string] $Product, [string] $Version, [string] $Sha256)
    return "$Product|$Version|$Sha256"
}

function ConvertTo-HexLower {
    param([byte[]] $Bytes)
    return -join ($Bytes | ForEach-Object { $_.ToString('x2') })
}

function Invoke-OtaSignCanonical {
    param(
        [Parameter(Mandatory)] [string] $ProjectRoot,
        [Parameter(Mandatory)] [string] $Canonical
    )
    $ossl = Get-OpenSslPath
    if (-not $ossl) { throw "openssl nao encontrado (instale Git for Windows ou openssl no PATH)." }
    $key = Get-SigningKeyPath -ProjectRoot $ProjectRoot
    if (-not (Test-Path -LiteralPath $key)) {
        throw "Chave privada ausente: $key (rode rbesp keygen ou defina RIBANENSE_SIGNING_KEY)."
    }
    $tmp = Join-Path $env:TEMP ("rbn-canon-{0}.txt" -f [guid]::NewGuid().ToString('N'))
    $sig = "$tmp.sig"
    try {
        [System.IO.File]::WriteAllBytes($tmp, [System.Text.Encoding]::ASCII.GetBytes($Canonical))
        & $ossl dgst -sha256 -sign $key -out $sig $tmp
        if ($LASTEXITCODE -ne 0) { throw "openssl dgst -sign falhou." }
        return (ConvertTo-HexLower -Bytes ([System.IO.File]::ReadAllBytes($sig)))
    }
    finally {
        foreach ($p in @($tmp, $sig)) {
            if ($p) { try { [System.IO.File]::Delete($p) } catch { } }
        }
    }
}

function Invoke-OtaVerifyCanonical {
    param(
        [Parameter(Mandatory)] [string] $ProjectRoot,
        [Parameter(Mandatory)] [string] $Canonical,
        [Parameter(Mandatory)] [string] $SigHex
    )
    $ossl = Get-OpenSslPath
    if (-not $ossl) { throw "openssl nao encontrado." }
    $pub = Join-Path $ProjectRoot 'secrets\ribanense-ota.pub.pem'
    if (-not (Test-Path -LiteralPath $pub)) {
        throw "Chave publica ausente: $pub"
    }
    if ($SigHex.Length % 2 -ne 0) { throw "assinatura hex invalida." }
    $bytes = New-Object byte[] ($SigHex.Length / 2)
    for ($i = 0; $i -lt $bytes.Length; $i++) {
        $bytes[$i] = [Convert]::ToByte($SigHex.Substring($i * 2, 2), 16)
    }
    $tmp = Join-Path $env:TEMP ("rbn-canon-{0}.txt" -f [guid]::NewGuid().ToString('N'))
    $sig = "$tmp.sig"
    try {
        [System.IO.File]::WriteAllBytes($tmp, [System.Text.Encoding]::ASCII.GetBytes($Canonical))
        [System.IO.File]::WriteAllBytes($sig, $bytes)
        & $ossl dgst -sha256 -verify $pub -signature $sig $tmp | Out-Null
        return ($LASTEXITCODE -eq 0)
    }
    finally {
        foreach ($p in @($tmp, $sig)) {
            if ($p) { try { [System.IO.File]::Delete($p) } catch { } }
        }
    }
}

function Set-FirmwareManifestPointer {
    param(
        [Parameter(Mandatory)] [string] $ProjectRoot,
        [Parameter(Mandatory)] [string] $Version,
        [Parameter(Mandatory)] [string] $Url,
        [Parameter(Mandatory)] [string] $Sha256
    )
    $fw = Join-Path $ProjectRoot 'firmware\ribanense-esp\firmware.json'
    $info = Get-OsVersionInfo -ProjectRoot $ProjectRoot
    $product = [string] $info.product
    if (-not $product) { $product = 'RibanenseESP' }
    $sig = Invoke-OtaSignCanonical -ProjectRoot $ProjectRoot -Canonical (Get-OtaCanonical -Product $product -Version $Version -Sha256 $Sha256)
    $doc = [ordered]@{
        schemaVersion = 1
        product       = $product
        version       = $Version
        minFlashMb    = 4
        url           = $Url
        sha256        = $Sha256
        sig           = $sig
    }
    ($doc | ConvertTo-Json -Depth 4) + "`n" | Set-Content -LiteralPath $fw -Encoding UTF8
    return $sig
}

function Get-GithubOwnerRepo {
    param([Parameter(Mandatory)] [string] $ProjectRoot)
    $id = Get-ProjectIdentity -ProjectRoot $ProjectRoot
    return [pscustomobject]@{ Owner = $id.Owner; Repo = $id.Repo }
}

function Get-ProjectIdentity {
    param([Parameter(Mandatory)] [string] $ProjectRoot)
    $info = $null
    try { $info = Get-OsVersionInfo -ProjectRoot $ProjectRoot } catch { $info = $null }
    $owner = if ($info -and $info.githubOwner) { [string] $info.githubOwner } else { 'alpha6678' }
    $repo = if ($info -and $info.githubRepo) { [string] $info.githubRepo } else { 'RibanenseESP' }
    $email = if ($info -and $info.gitEmail) { [string] $info.gitEmail } else { 'dionerdfrg3@gmail.com' }
    return [pscustomobject]@{
        Owner   = $owner
        Repo    = $repo
        Name    = $owner
        Email   = $email
        Remote  = "https://github.com/$owner/$repo.git"
    }
}

function Get-GhActiveUser {
    if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
        return $null
    }
    $login = (& gh api user --jq '.login' 2>$null)
    if ($login) { return [string] $login }
    $status = & gh auth status 2>&1 | Out-String
    if ($status -match 'Logged in to github.com account (\S+) \(keyring\)\s+[\s\S]*?Active account: true') {
        return $Matches[1]
    }
    return $null
}

function Sync-ProjectIdentity {
    param(
        [Parameter(Mandatory)] [string] $ProjectRoot,
        [switch] $Quiet
    )
    $id = Get-ProjectIdentity -ProjectRoot $ProjectRoot
    $changed = @()
    Push-Location $ProjectRoot
    try {
        $curName = (& git config --local --get user.name 2>$null)
        $curEmail = (& git config --local --get user.email 2>$null)
        if ($curName -ne $id.Name) {
            & git config --local user.name $id.Name
            $changed += "user.name=$($id.Name)"
        }
        if ($curEmail -ne $id.Email) {
            & git config --local user.email $id.Email
            $changed += "user.email=$($id.Email)"
        }
        $url = (& git remote get-url origin 2>$null)
        if ($LASTEXITCODE -eq 0 -and $url -and $url -ne $id.Remote) {
            if ($url -notmatch [regex]::Escape("$($id.Owner)/$($id.Repo)")) {
                throw "origin aponta para $url. Este repo e $($id.Owner)/$($id.Repo)."
            }
            & git remote set-url origin $id.Remote
            $changed += "origin=$($id.Remote)"
        }
        $helperCmd = Join-Path $PSScriptRoot 'git-credential-ribanense.cmd'
        $helperUnix = ($helperCmd -replace '\\', '/')
        $wantHelper = '!"' + $helperUnix + '"'
        $helpers = @(& git config --local --get-all credential.helper 2>$null)
        $joined = ($helpers -join '|')
        if ($joined -notmatch 'git-credential-ribanense') {
            & git config --local --unset-all credential.helper 2>$null
            & git config --local credential.helper $wantHelper
            $changed += 'credential.helper=ribanense'
        }
        $credUser = (& git config --local --get credential.https://github.com.username 2>$null)
        if ($credUser -ne $id.Owner) {
            & git config --local credential.https://github.com.username $id.Owner
            $changed += "credential.username=$($id.Owner)"
        }
    } finally {
        Pop-Location
    }
    if (-not $Quiet -and $changed.Count -gt 0) {
        Write-Host ("Identidade do projeto: " + ($changed -join '; ')) -ForegroundColor Cyan
    }
    return $id
}

function Invoke-WithProjectGithub {
    param(
        [Parameter(Mandatory)] [string] $ProjectRoot,
        [Parameter(Mandatory)] [scriptblock] $Script
    )
    $id = Get-ProjectIdentity -ProjectRoot $ProjectRoot
    if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
        throw "GitHub CLI (gh) nao encontrado."
    }
    $prev = Get-GhActiveUser
    $switched = $false
    if ($prev -ne $id.Owner) {
        & gh auth switch --user $id.Owner
        if ($LASTEXITCODE -ne 0) {
            throw "gh precisa da conta $($id.Owner) neste repo. Entre com: gh auth login"
        }
        $switched = $true
        Write-Host "gh -> $($id.Owner) (volta para $prev no fim)" -ForegroundColor Cyan
    }
    try {
        & $Script
    } finally {
        if ($switched -and $prev) {
            & gh auth switch --user $prev 2>$null | Out-Null
        }
    }
}

function Invoke-ProjectGh {
    param(
        [Parameter(Mandatory)] [string] $ProjectRoot,
        [Parameter(Mandatory)] [string[]] $GhArgs
    )
    $toRun = $GhArgs
    Invoke-WithProjectGithub -ProjectRoot $ProjectRoot -Script {
        & gh @toRun
        if ($LASTEXITCODE -ne 0) {
            throw "gh $($toRun -join ' ') falhou (codigo $LASTEXITCODE)."
        }
    }.GetNewClosure()
}

function Write-Sha256Sidecar {
    param([Parameter(Mandatory)] [string] $FilePath)
    $hash = (Get-FileHash -LiteralPath $FilePath -Algorithm SHA256).Hash.ToLowerInvariant()
    $name = [System.IO.Path]::GetFileName($FilePath)
    $shaPath = "$FilePath.sha256"
    "$hash  $name" | Set-Content -LiteralPath $shaPath -Encoding ASCII
    return $hash
}

function New-StoredZip {
    param(
        [Parameter(Mandatory)] [string] $ZipPath,
        [Parameter(Mandatory)] [hashtable] $Entries
    )
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    if (Test-Path -LiteralPath $ZipPath) {
        Remove-Item -LiteralPath $ZipPath -Force
    }
    $zip = [System.IO.Compression.ZipFile]::Open($ZipPath, 'Create')
    try {
        foreach ($name in $Entries.Keys) {
            $src = [string] $Entries[$name]
            [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                $zip, $src, $name, [System.IO.Compression.CompressionLevel]::NoCompression)
        }
    }
    finally {
        $zip.Dispose()
    }
}
