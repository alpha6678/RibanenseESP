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

function Get-GithubOwnerRepo {
    param([Parameter(Mandatory)] [string] $ProjectRoot)
    $owner = 'alpha6678'
    $repo = 'RibanenseESP'
    Push-Location $ProjectRoot
    try {
        $url = & git remote get-url origin 2>$null
        # Aceita github.com e alias SSH (github.com-pessoal).
        if ($LASTEXITCODE -eq 0 -and $url -match 'github\.com(?:-[^:/]+)?[:/]([^/]+)/([^/.]+)') {
            $owner = $Matches[1]
            $repo = $Matches[2]
        }
    } finally {
        Pop-Location
    }
    return [pscustomobject]@{ Owner = $owner; Repo = $repo }
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
