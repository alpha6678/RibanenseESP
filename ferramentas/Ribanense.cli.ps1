#Requires -Version 5.1
# Ribanense.cli.ps1 — CLI de desenvolvimento do monorepo Ribanense Soluções.

param(
    [Parameter(Position = 0)]
    [string] $Command = 'help',

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $Rest
)

$ErrorActionPreference = 'Stop'

# ---------- Ambiente ----------

$script:CliRoot = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
$script:ProjectRoot = Split-Path -Parent $script:CliRoot
$script:SolutionPath = Join-Path $script:ProjectRoot 'Ribanense.Solucoes.slnx'
$script:LauncherProjectPath = Join-Path $script:ProjectRoot 'src\Ribanense.Solucoes.Launcher\Ribanense.Solucoes.Launcher.csproj'
$script:AppsRoot = Join-Path $script:ProjectRoot 'src\aplicativos'
$script:EspAppsRoot = Join-Path $script:ProjectRoot 'firmware\apps'
$script:OsRoot = Join-Path $script:ProjectRoot 'firmware\ribanense-esp'
$script:EspSdkRoot = Join-Path $script:ProjectRoot 'firmware\esp-sdk'
$script:OsVersionHeader = Join-Path $script:EspSdkRoot 'components\board\include\ribanense_esp_version.h'
$script:OsFirmwareJson = Join-Path $script:OsRoot 'firmware.json'
$script:EspCatalogPath = Join-Path $script:ProjectRoot 'catalog\esp-catalog.json'
$script:LauncherDataRoot = Join-Path $env:LOCALAPPDATA 'Ribanense Soluções'
$script:DevLinkRoot = Join-Path $script:LauncherDataRoot 'aplicativos'

$script:RestArguments = @(
    foreach ($Argument in $Rest) {
        if (-not [string]::IsNullOrWhiteSpace($Argument)) {
            $Argument
        }
    }
)

# ---------- Saida colorida ----------

function Write-Ok    { param([string] $m) Write-Host "[OK] $m"  -ForegroundColor Green }
function Write-Err   { param([string] $m) Write-Host "[ERR] $m" -ForegroundColor Red }
function Write-Info  { param([string] $m) Write-Host "[..] $m"  -ForegroundColor Cyan }
function Write-Warn2 { param([string] $m) Write-Host "[!!] $m"  -ForegroundColor Yellow }
function Write-Muted { param([string] $m) Write-Host "     $m"  -ForegroundColor DarkGray }
function Write-Step  { param([string] $m) Write-Host ""; Write-Host "=== $m ===" -ForegroundColor Magenta }

# ---------- Helpers basicos ----------

function Assert-PathExists {
    param([Parameter(Mandatory)] [string] $Path, [Parameter(Mandatory)] [string] $Description)
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Description nao encontrado: $Path"
    }
}

function Assert-CommandAvailable {
    param([Parameter(Mandatory)] [string] $CommandName)
    if (-not (Get-Command $CommandName -ErrorAction SilentlyContinue)) {
        throw "Comando nao encontrado no PATH: $CommandName"
    }
}

function Get-AppProjectPath {
    param([Parameter(Mandatory)] [string] $AppName)
    $projName = "Ribanense.Solucoes.App.$AppName"
    $projPath = Join-Path (Join-Path $script:AppsRoot $projName) "$projName.csproj"
    if (-not (Test-Path -LiteralPath $projPath)) {
        throw "App '$AppName' nao encontrado em src\aplicativos\. Use 'rb list' para ver apps disponiveis."
    }
    return $projPath
}

function Get-AppOutputExe {
    param([Parameter(Mandatory)] [string] $AppName)
    $projName = "Ribanense.Solucoes.App.$AppName"
    return Join-Path $script:AppsRoot "$projName\bin\Debug\net10.0-windows\$projName.exe"
}

function Get-AppOutputDir {
    param([Parameter(Mandatory)] [string] $AppName)
    $projName = "Ribanense.Solucoes.App.$AppName"
    return Join-Path $script:AppsRoot "$projName\bin\Debug\net10.0-windows"
}

function Get-RibananseProcessesFromRepo {
    param([string[]] $NameFilters = @('Ribanense.Solucoes.Launcher', 'Ribanense.Solucoes.App.*'))
    $root = [System.IO.Path]::GetFullPath($script:ProjectRoot)
    $list = @()
    foreach ($filter in $NameFilters) {
        foreach ($p in (Get-Process -Name $filter -ErrorAction SilentlyContinue)) {
            $exePath = $null
            try {
                $exePath = $p.Path
                if (-not $exePath -and $p.MainModule) { $exePath = $p.MainModule.FileName }
            } catch { }

            if ([string]::IsNullOrWhiteSpace($exePath)) {
                $list += $p
            } else {
                $exeFull = [System.IO.Path]::GetFullPath($exePath)
                if ($exeFull.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
                    $list += $p
                }
            }
        }
    }
    return $list
}

function Stop-RibananseProcesses {
    param([string[]] $NameFilters = @('Ribanense.Solucoes.Launcher', 'Ribanense.Solucoes.App.*'))
    $procs = Get-RibananseProcessesFromRepo -NameFilters $NameFilters
    if ($procs.Count -eq 0) { return }
    foreach ($p in $procs) {
        Write-Warn2 "Encerrando instancia (PID $($p.Id), $($p.ProcessName))..."
        try { Stop-Process -Id $p.Id -Force -ErrorAction Stop }
        catch { Write-Warn2 "Nao foi possivel encerrar PID $($p.Id) (pode estar elevado)." }
    }
    Start-Sleep -Milliseconds 400
}

function Invoke-DotNet {
    param([Parameter(Mandatory)] [string[]] $Arguments)
    Assert-CommandAvailable 'dotnet'
    Push-Location $script:ProjectRoot
    try {
        & dotnet @Arguments
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
    finally { Pop-Location }
}

function Get-AllAppProjects {
    if (-not (Test-Path -LiteralPath $script:AppsRoot)) { return @() }
    $results = @()
    foreach ($dir in Get-ChildItem -LiteralPath $script:AppsRoot -Directory -ErrorAction SilentlyContinue) {
        $projName = $dir.Name
        if ($projName -notlike 'Ribanense.Solucoes.App.*') { continue }
        $csproj = Join-Path $dir.FullName "$projName.csproj"
        if (-not (Test-Path -LiteralPath $csproj)) { continue }

        $shortName = $projName.Substring('Ribanense.Solucoes.App.'.Length)
        $version = $null
        try {
            [xml] $xml = Get-Content -LiteralPath $csproj -Raw
            $verNode = $xml.SelectSingleNode('//PropertyGroup/Version')
            if ($verNode) { $version = $verNode.InnerText.Trim() }
        } catch { }

        $manifestPath = Join-Path $dir.FullName 'app.json'
        $manifestVersion = $null
        $publicName = $null
        $githubTagPrefix = $null
        if (Test-Path -LiteralPath $manifestPath) {
            try {
                $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json -ErrorAction SilentlyContinue
                if ($manifest) {
                    $manifestVersion = $manifest.version
                    $publicName = if ($manifest.publicName) { $manifest.publicName } else { $manifest.name }
                    $githubTagPrefix = $manifest.githubTagPrefix
                }
            } catch { }
        }

        $results += [pscustomobject]@{
            ShortName       = $shortName
            ProjectName     = $projName
            ProjectPath     = $csproj
            CsprojVersion   = $version
            ManifestVersion = $manifestVersion
            ManifestPath    = $manifestPath
            PublicName      = $publicName
            GithubTagPrefix = $githubTagPrefix
            HasManifest     = (Test-Path -LiteralPath $manifestPath)
            Directory       = $dir.FullName
        }
    }
    return $results
}

function Get-SdkVersion {
    $sdkFile = Join-Path $script:ProjectRoot 'src\Ribanense.Solucoes.PluginSDK\SdkVersion.cs'
    if (-not (Test-Path -LiteralPath $sdkFile)) { return '0.0.0' }
    $content = Get-Content -LiteralPath $sdkFile -Raw
    if ($content -match '"(\d+\.\d+\.\d+(?:-[\w\.-]+)?)"') {
        return $Matches[1]
    }
    return '0.0.0'
}

function Resolve-AppShortName {
    param([Parameter(Mandatory)] [string] $AppInput)

    if ([string]::IsNullOrWhiteSpace($AppInput)) {
        throw "Nome do app vazio. Use 'rb list' para ver apps disponiveis."
    }

    $apps = @(Get-AllAppProjects)
    if ($apps.Count -eq 0) {
        throw "Nenhum app encontrado em src\aplicativos\."
    }

    $token = $AppInput.Trim()
    $exact = @($apps | Where-Object { $_.ShortName.Equals($token, [System.StringComparison]::OrdinalIgnoreCase) })
    if ($exact.Count -eq 1) {
        return $exact[0].ShortName
    }

    $starts = @($apps | Where-Object { $_.ShortName.StartsWith($token, [System.StringComparison]::OrdinalIgnoreCase) })
    if ($starts.Count -eq 1) {
        Write-Muted "Assumindo app '$($starts[0].ShortName)' para '$AppInput'."
        return $starts[0].ShortName
    }

    $contains = @($apps | Where-Object { $_.ShortName.IndexOf($token, [System.StringComparison]::OrdinalIgnoreCase) -ge 0 })
    $suggestions = @($exact + $starts + $contains | Select-Object -ExpandProperty ShortName -Unique)

    if ($suggestions.Count -gt 0) {
        throw "App '$AppInput' nao encontrado. Quis dizer: $($suggestions -join ', ')? Use 'rb list'."
    }

    throw "App '$AppInput' nao encontrado em src\aplicativos\. Use 'rb list' para ver apps disponiveis."
}

function Test-SimpleSemVer {
    param([Parameter(Mandatory)] [string] $Version)
    return [regex]::IsMatch($Version.Trim(), '^\d+\.\d+\.\d+$')
}

function Get-NextPatchVersion {
    param([Parameter(Mandatory)] [string] $Version)
    if (-not (Test-SimpleSemVer -Version $Version)) {
        throw "Versao '$Version' fora do formato suportado para bump automatico (esperado: major.minor.patch)."
    }
    $parts = $Version.Split('.')
    $major = [int] $parts[0]
    $minor = [int] $parts[1]
    $patch = [int] $parts[2] + 1
    return "$major.$minor.$patch"
}

function Get-AssemblyVersionFromSemVer {
    param([Parameter(Mandatory)] [string] $Version)
    if (-not (Test-SimpleSemVer -Version $Version)) {
        throw "Versao '$Version' invalida para AssemblyVersion."
    }
    return "$Version.0"
}

function Get-AppTagPrefix {
    param([Parameter(Mandatory)] $App)
    if ($App.GithubTagPrefix -and -not [string]::IsNullOrWhiteSpace($App.GithubTagPrefix)) {
        return $App.GithubTagPrefix.Trim()
    }
    return "$($App.ShortName.ToLowerInvariant())-v"
}

function Get-LatestTagForPrefix {
    param([Parameter(Mandatory)] [string] $TagPrefix)

    $tagFilter = "$TagPrefix*"
    Push-Location $script:ProjectRoot
    try {
        $rawTags = @(& git tag --list $tagFilter)
        if ($LASTEXITCODE -ne 0) {
            throw "Falha ao listar tags para prefixo '$TagPrefix'."
        }
    }
    finally {
        Pop-Location
    }

    $parsed = @()
    foreach ($tag in $rawTags) {
        if ([string]::IsNullOrWhiteSpace($tag)) { continue }
        if (-not $tag.StartsWith($TagPrefix, [System.StringComparison]::Ordinal)) { continue }
        $version = $tag.Substring($TagPrefix.Length)
        if (-not (Test-SimpleSemVer -Version $version)) { continue }
        $parts = $version.Split('.')
        $parsed += [pscustomobject]@{
            Tag    = $tag
            Major  = [int] $parts[0]
            Minor  = [int] $parts[1]
            Patch  = [int] $parts[2]
            SemVer = $version
        }
    }

    if ($parsed.Count -eq 0) { return $null }

    return ($parsed |
        Sort-Object -Property @{ Expression = { $_.Major }; Descending = $true },
                              @{ Expression = { $_.Minor }; Descending = $true },
                              @{ Expression = { $_.Patch }; Descending = $true } |
        Select-Object -First 1).Tag
}

function Get-ChangedFilesSinceTag {
    param([Parameter(Mandatory)] [string] $Tag)

    Push-Location $script:ProjectRoot
    try {
        $files = @(& git diff --name-only "$Tag..HEAD")
        if ($LASTEXITCODE -ne 0) {
            throw "Falha ao ler arquivos alterados desde '$Tag'."
        }
    }
    finally {
        Pop-Location
    }
    return @(
        foreach ($f in $files) {
            $t = $f.Trim()
            if (-not [string]::IsNullOrWhiteSpace($t)) {
                $t.Replace('\', '/')
            }
        }
    )
}

function Get-AppRelevantPathPrefixes {
    param([Parameter(Mandatory)] [string] $AppShortName)
    return @(
        "src/aplicativos/Ribanense.Solucoes.App.$AppShortName/",
        "tests/Ribanense.Solucoes.App.$AppShortName.Tests/",
        "src/Ribanense.Solucoes.PluginSDK/",
        "src/Ribanense.Solucoes.Infrastructure/",
        "src/Ribanense.Solucoes.UI/"
    )
}

function Get-LauncherRelevantPathPrefixes {
    return @(
        "src/Ribanense.Solucoes.Launcher/",
        "tests/Ribanense.Solucoes.Launcher.Tests/",
        "catalog/catalog.json",
        "catalog/icons/",
        "src/Ribanense.Solucoes.PluginSDK/",
        "src/Ribanense.Solucoes.Infrastructure/",
        "src/Ribanense.Solucoes.UI/",
        "Directory.Build.props"
    )
}

function Get-OsIrrelevantPathPrefixes {
    return @(
        'firmware/ribanense-esp/firmware.json',
        'firmware/ribanense-esp/dist/'
    )
}

function Get-EspAppIrrelevantPathPrefixes {
    return @(
        'firmware/esp-sdk/components/board/include/ribanense_esp_version.h'
    )
}

function Test-PathMatchesAnyPrefix {
    param(
        [Parameter(Mandatory)] [string] $Path,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [string[]] $Prefixes
    )
    foreach ($prefix in $Prefixes) {
        if ([string]::IsNullOrWhiteSpace($prefix)) { continue }
        if ($Path.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }
    return $false
}

function Select-FilesByPrefixes {
    param(
        [Parameter(Mandatory)] [string[]] $Files,
        [Parameter(Mandatory)] [string[]] $Prefixes,
        [string[]] $Excludes = @()
    )
    $matched = @()
    foreach ($file in $Files) {
        $normalized = $file.Replace('\', '/')
        if (($Excludes.Count -gt 0) -and (Test-PathMatchesAnyPrefix -Path $normalized -Prefixes $Excludes)) {
            continue
        }
        if (Test-PathMatchesAnyPrefix -Path $normalized -Prefixes $Prefixes) {
            $matched += $normalized
        }
    }
    return @($matched | Select-Object -Unique)
}

function Get-AppVersionState {
    param([Parameter(Mandatory)] $App)

    [xml] $csprojXml = Get-Content -LiteralPath $App.ProjectPath -Raw
    $versionNode = $csprojXml.SelectSingleNode('//PropertyGroup/Version')
    $assemblyNode = $csprojXml.SelectSingleNode('//PropertyGroup/AssemblyVersion')
    $fileNode = $csprojXml.SelectSingleNode('//PropertyGroup/FileVersion')

    $csprojVersion = if ($versionNode) { $versionNode.InnerText.Trim() } else { $null }

    $manifestVersion = $null
    if ($App.HasManifest -and (Test-Path -LiteralPath $App.ManifestPath)) {
        $manifest = Get-Content -LiteralPath $App.ManifestPath -Raw | ConvertFrom-Json -ErrorAction SilentlyContinue
        if ($manifest) { $manifestVersion = $manifest.version }
    }

    return [pscustomobject]@{
        CsprojVersion      = $csprojVersion
        ManifestVersion    = $manifestVersion
        HasAssemblyVersion = ($null -ne $assemblyNode)
        HasFileVersion     = ($null -ne $fileNode)
    }
}

function Set-FirstXmlTagValue {
    param(
        [Parameter(Mandatory)] [string] $Content,
        [Parameter(Mandatory)] [string] $TagName,
        [Parameter(Mandatory)] [string] $NewValue
    )

    $pattern = "(?s)(<$TagName>)([^<]*)(</$TagName>)"
    if (-not [regex]::IsMatch($Content, $pattern)) {
        throw "Tag <$TagName> nao encontrada para atualizacao."
    }
    return [regex]::Replace(
        $Content,
        $pattern,
        { param($m) "$($m.Groups[1].Value)$NewValue$($m.Groups[3].Value)" },
        1
    )
}

function Set-AppVersion {
    param(
        [Parameter(Mandatory)] $App,
        [Parameter(Mandatory)] [string] $Version
    )

    if (-not (Test-SimpleSemVer -Version $Version)) {
        throw "Versao '$Version' invalida para atualizacao automatica."
    }

    $versionState = Get-AppVersionState -App $App
    if (-not $versionState.CsprojVersion) {
        throw "Nao foi possivel ler <Version> no projeto '$($App.ProjectPath)'."
    }
    if (-not $App.HasManifest) {
        throw "App '$($App.ShortName)' sem app.json. Fluxo publish all exige manifesto."
    }
    if ($versionState.ManifestVersion -ne $versionState.CsprojVersion) {
        throw "Divergencia de versao em '$($App.ShortName)': csproj=$($versionState.CsprojVersion) app.json=$($versionState.ManifestVersion)."
    }

    $assemblyVersion = Get-AssemblyVersionFromSemVer -Version $Version

    $csprojContent = Get-Content -LiteralPath $App.ProjectPath -Raw
    $csprojContent = Set-FirstXmlTagValue -Content $csprojContent -TagName 'Version' -NewValue $Version
    if ($versionState.HasAssemblyVersion) {
        $csprojContent = Set-FirstXmlTagValue -Content $csprojContent -TagName 'AssemblyVersion' -NewValue $assemblyVersion
    }
    if ($versionState.HasFileVersion) {
        $csprojContent = Set-FirstXmlTagValue -Content $csprojContent -TagName 'FileVersion' -NewValue $assemblyVersion
    }
    Set-Content -LiteralPath $App.ProjectPath -Value $csprojContent -Encoding UTF8

    $manifestContent = Get-Content -LiteralPath $App.ManifestPath -Raw
    $manifestPattern = '("version"\s*:\s*")[^"]+(")'
    if (-not [regex]::IsMatch($manifestContent, $manifestPattern)) {
        throw "Campo 'version' nao encontrado em '$($App.ManifestPath)'."
    }
    $manifestContent = [regex]::Replace(
        $manifestContent,
        $manifestPattern,
        { param($m) "$($m.Groups[1].Value)$Version$($m.Groups[2].Value)" },
        1
    )
    Set-Content -LiteralPath $App.ManifestPath -Value $manifestContent -Encoding UTF8
}

function Get-LauncherVersionState {
    $buildProps = Join-Path $script:ProjectRoot "Directory.Build.props"
    if (-not (Test-Path -LiteralPath $buildProps)) {
        return [pscustomobject]@{
            BuildPropsPath     = $buildProps
            CsprojVersion      = $null
            HasAssemblyVersion = $false
            HasFileVersion     = $false
        }
    }
    try {
        [xml] $xml = Get-Content -LiteralPath $buildProps -Raw
        $verNode = $xml.SelectSingleNode("//PropertyGroup/Version")
        $asmNode = $xml.SelectSingleNode("//PropertyGroup/AssemblyVersion")
        $fileNode = $xml.SelectSingleNode("//PropertyGroup/FileVersion")
        return [pscustomobject]@{
            BuildPropsPath     = $buildProps
            CsprojVersion      = if ($verNode) { $verNode.InnerText.Trim() } else { $null }
            HasAssemblyVersion = ($null -ne $asmNode)
            HasFileVersion     = ($null -ne $fileNode)
        }
    } catch {
        return [pscustomobject]@{
            BuildPropsPath     = $buildProps
            CsprojVersion      = $null
            HasAssemblyVersion = $false
            HasFileVersion     = $false
        }
    }
}

function Set-LauncherVersion {
    param([Parameter(Mandatory)] [string] $Version)

    if (-not (Test-SimpleSemVer -Version $Version)) {
        throw "Versao '$Version' invalida para atualizacao automatica (Launcher / Directory.Build.props)."
    }

    $versionState = Get-LauncherVersionState
    if (-not $versionState.CsprojVersion) {
        throw "Nao foi possivel ler <Version> em Directory.Build.props."
    }

    $assemblyVersion = Get-AssemblyVersionFromSemVer -Version $Version
    $buildProps = $versionState.BuildPropsPath
    $content = Get-Content -LiteralPath $buildProps -Raw
    $content = Set-FirstXmlTagValue -Content $content -TagName "Version" -NewValue $Version
    if ($versionState.HasAssemblyVersion) {
        $content = Set-FirstXmlTagValue -Content $content -TagName "AssemblyVersion" -NewValue $assemblyVersion
    }
    if ($versionState.HasFileVersion) {
        $content = Set-FirstXmlTagValue -Content $content -TagName "FileVersion" -NewValue $assemblyVersion
    }
    Set-Content -LiteralPath $buildProps -Value $content -Encoding UTF8
}

function Get-PublishAllCandidates {
    param([Parameter(Mandatory)] [object[]] $Apps)

    $plan = @()
    foreach ($app in $Apps) {
        $tagPrefix = Get-AppTagPrefix -App $app
        $latestTag = Get-LatestTagForPrefix -TagPrefix $tagPrefix

        $include = $false
        $matchedFiles = @()
        $reason = ''

        if ([string]::IsNullOrWhiteSpace($latestTag)) {
            $include = $true
            $reason = 'sem tag anterior para o prefixo'
        } else {
            $changedFiles = Get-ChangedFilesSinceTag -Tag $latestTag
            $prefixes = Get-AppRelevantPathPrefixes -AppShortName $app.ShortName
            $matchedFiles = Select-FilesByPrefixes -Files $changedFiles -Prefixes $prefixes
            if ($matchedFiles.Count -gt 0) {
                $include = $true
                $reason = "arquivos alterados desde $latestTag"
            }
        }

        if (-not $include) { continue }

        $versionState = Get-AppVersionState -App $app
        if (-not $versionState.CsprojVersion) {
            throw "Versao do csproj nao encontrada para app '$($app.ShortName)'."
        }
        if (-not (Test-SimpleSemVer -Version $versionState.CsprojVersion)) {
            throw "App '$($app.ShortName)' com versao '$($versionState.CsprojVersion)' fora do formato major.minor.patch."
        }
        if ($app.HasManifest -and ($versionState.ManifestVersion -ne $versionState.CsprojVersion)) {
            throw "App '$($app.ShortName)' com versoes divergentes (csproj=$($versionState.CsprojVersion), app.json=$($versionState.ManifestVersion))."
        }

        $next = Get-NextPatchVersion -Version $versionState.CsprojVersion
        $plan += [pscustomobject]@{
            App         = $app
            Kind        = 'win-app'
            Current     = $versionState.CsprojVersion
            Next        = $next
            TagPrefix   = $tagPrefix
            Tag         = "$tagPrefix$next"
            LatestTag   = $latestTag
            Reason      = $reason
            ChangedHint = @($matchedFiles | Select-Object -First 5)
        }
    }

    return $plan
}

function Get-PublishAllLauncherPlanItem {
    if (-not (Test-Path -LiteralPath $script:LauncherProjectPath)) {
        return $null
    }

    $versionState = Get-LauncherVersionState
    if (-not $versionState.CsprojVersion) {
        throw "Versao do Launcher nao encontrada em Directory.Build.props."
    }
    if (-not (Test-SimpleSemVer -Version $versionState.CsprojVersion)) {
        throw "Launcher com versao '$($versionState.CsprojVersion)' fora do formato major.minor.patch (ajuste Directory.Build.props)."
    }

    $tagPrefix = "launcher-v"
    $latestTag = Get-LatestTagForPrefix -TagPrefix $tagPrefix

    $include = $false
    $matchedFiles = @()
    $reason = ""

    if ([string]::IsNullOrWhiteSpace($latestTag)) {
        $include = $true
        $reason = "sem tag anterior para o prefixo"
    } else {
        $changedFiles = Get-ChangedFilesSinceTag -Tag $latestTag
        $prefixes = Get-LauncherRelevantPathPrefixes
        $matchedFiles = Select-FilesByPrefixes -Files $changedFiles -Prefixes $prefixes
        if ($matchedFiles.Count -gt 0) {
            $include = $true
            $reason = "arquivos alterados desde $latestTag"
        }
    }

    if (-not $include) { return $null }

    $next = Get-NextPatchVersion -Version $versionState.CsprojVersion
    $launcherStub = [pscustomobject]@{ ShortName = "Launcher" }

    return [pscustomobject]@{
        App         = $launcherStub
        Kind        = 'launcher'
        Current     = $versionState.CsprojVersion
        Next        = $next
        TagPrefix   = $tagPrefix
        Tag         = "$tagPrefix$next"
        LatestTag   = $latestTag
        Reason      = $reason
        ChangedHint = @($matchedFiles | Select-Object -First 5)
    }
}

function Get-LauncherVersion {
    $state = Get-LauncherVersionState
    if (-not $state.CsprojVersion) {
        return "0.0.0"
    }
    return $state.CsprojVersion
}

function Get-OsVersion {
    if (-not (Test-Path -LiteralPath $script:OsVersionHeader)) {
        return $null
    }
    $content = Get-Content -LiteralPath $script:OsVersionHeader -Raw
    if ($content -match '#define\s+RIBANENSEESP_VERSION\s+"([^"]+)"') {
        return $Matches[1]
    }
    return $null
}

function Set-OsVersion {
    param([Parameter(Mandatory)] [string] $Version)
    if (-not (Test-SimpleSemVer -Version $Version)) {
        throw "Versao '$Version' invalida para o OS."
    }
    $header = Get-Content -LiteralPath $script:OsVersionHeader -Raw
    $headerPattern = '(#define\s+RIBANENSEESP_VERSION\s+")[^"]+(")'
    if (-not [regex]::IsMatch($header, $headerPattern)) {
        throw "RIBANENSEESP_VERSION nao encontrado em $($script:OsVersionHeader)."
    }
    $header = [regex]::Replace($header, $headerPattern, { param($m) "$($m.Groups[1].Value)$Version$($m.Groups[2].Value)" }, 1)
    Set-Content -LiteralPath $script:OsVersionHeader -Value $header -Encoding UTF8

    if (Test-Path -LiteralPath $script:OsFirmwareJson) {
        $json = Get-Content -LiteralPath $script:OsFirmwareJson -Raw
        $json = [regex]::Replace($json, '("version"\s*:\s*")[^"]+(")', { param($m) "$($m.Groups[1].Value)$Version$($m.Groups[2].Value)" }, 1)
        Set-Content -LiteralPath $script:OsFirmwareJson -Value $json -Encoding UTF8
    }
}

function Get-AllEspAppProjects {
    if (-not (Test-Path -LiteralPath $script:EspAppsRoot)) { return @() }
    $results = @()
    foreach ($dir in Get-ChildItem -LiteralPath $script:EspAppsRoot -Directory -ErrorAction SilentlyContinue) {
        $manifestPath = Join-Path $dir.FullName 'app.json'
        if (-not (Test-Path -LiteralPath $manifestPath)) { continue }
        $manifest = $null
        try { $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json } catch { }
        if (-not $manifest) { continue }
        $results += [pscustomobject]@{
            ShortName       = $dir.Name
            Kind            = 'esp-app'
            ManifestVersion = [string] $manifest.version
            ManifestPath    = $manifestPath
            PublicName      = if ($manifest.publicName) { $manifest.publicName } else { $manifest.name }
            GithubTagPrefix = [string] $manifest.githubTagPrefix
            Id              = [string] $manifest.id
            Directory       = $dir.FullName
            CMakeListsPath  = (Join-Path $dir.FullName 'CMakeLists.txt')
            HasManifest     = $true
        }
    }
    return $results
}

function Resolve-EspAppShortName {
    param([Parameter(Mandatory)] [string] $AppInput)
    $apps = @(Get-AllEspAppProjects)
    $token = $AppInput.Trim()
    $exact = @($apps | Where-Object { $_.ShortName.Equals($token, [System.StringComparison]::OrdinalIgnoreCase) })
    if ($exact.Count -eq 1) { return $exact[0].ShortName }
    throw "App da placa '$AppInput' nao encontrado em firmware\apps\. Use 'rb list'."
}

function Test-IsOsTarget {
    param([string] $Name)
    return $Name -in @('OS', 'RibanenseESP', 'Esp')
}

function Set-EspAppVersion {
    param(
        [Parameter(Mandatory)] $App,
        [Parameter(Mandatory)] [string] $Version
    )
    if (-not (Test-SimpleSemVer -Version $Version)) {
        throw "Versao '$Version' invalida para app da placa."
    }
    $manifestContent = Get-Content -LiteralPath $App.ManifestPath -Raw
    $manifestContent = [regex]::Replace(
        $manifestContent,
        '("version"\s*:\s*")[^"]+(")',
        { param($m) "$($m.Groups[1].Value)$Version$($m.Groups[2].Value)" },
        1
    )
    Set-Content -LiteralPath $App.ManifestPath -Value $manifestContent -Encoding UTF8

    if (Test-Path -LiteralPath $App.CMakeListsPath) {
        $cmake = Get-Content -LiteralPath $App.CMakeListsPath -Raw
        if ($cmake -match 'set\(PROJECT_VER\s+"[^"]+"\)') {
            $cmake = [regex]::Replace($cmake, 'set\(PROJECT_VER\s+"[^"]+"\)', "set(PROJECT_VER `"$Version`")", 1)
            Set-Content -LiteralPath $App.CMakeListsPath -Value $cmake -Encoding UTF8
        }
    }
}

function Get-OsRelevantPathPrefixes {
    return @(
        'firmware/ribanense-esp/',
        'firmware/esp-sdk/'
    )
}

function Get-EspAppRelevantPathPrefixes {
    param([Parameter(Mandatory)] [string] $AppShortName)
    return @(
        "firmware/apps/$AppShortName/",
        'firmware/esp-sdk/'
    )
}

function Get-PublishAllOsPlanItem {
    if (-not (Test-Path -LiteralPath $script:OsRoot)) { return $null }
    $current = Get-OsVersion
    if (-not $current) {
        throw "Versao do OS nao encontrada em ribanense_esp_version.h."
    }
    if (-not (Test-SimpleSemVer -Version $current)) {
        throw "OS com versao '$current' fora de major.minor.patch."
    }
    $tagPrefix = 'ribanense-esp-v'
    $latestTag = Get-LatestTagForPrefix -TagPrefix $tagPrefix
    $include = $false
    $matchedFiles = @()
    $reason = ''
    if ([string]::IsNullOrWhiteSpace($latestTag)) {
        $include = $true
        $reason = 'sem tag anterior para o prefixo'
    } else {
        $changedFiles = Get-ChangedFilesSinceTag -Tag $latestTag
        $matchedFiles = Select-FilesByPrefixes -Files $changedFiles -Prefixes (Get-OsRelevantPathPrefixes) -Excludes (Get-OsIrrelevantPathPrefixes)
        if ($matchedFiles.Count -gt 0) {
            $include = $true
            $reason = "arquivos alterados desde $latestTag"
        }
    }
    if (-not $include) { return $null }
    $next = Get-NextPatchVersion -Version $current
    return [pscustomobject]@{
        App         = [pscustomobject]@{ ShortName = 'OS'; Kind = 'os' }
        Kind        = 'os'
        Current     = $current
        Next        = $next
        TagPrefix   = $tagPrefix
        Tag         = "$tagPrefix$next"
        LatestTag   = $latestTag
        Reason      = $reason
        ChangedHint = @($matchedFiles | Select-Object -First 5)
    }
}

function Get-PublishAllEspAppCandidates {
    $plan = @()
    foreach ($app in @(Get-AllEspAppProjects)) {
        $tagPrefix = if ($app.GithubTagPrefix) { $app.GithubTagPrefix } else { "esp-$($app.ShortName.ToLowerInvariant())-v" }
        $latestTag = Get-LatestTagForPrefix -TagPrefix $tagPrefix
        $include = $false
        $matchedFiles = @()
        $reason = ''
        if ([string]::IsNullOrWhiteSpace($latestTag)) {
            $include = $true
            $reason = 'sem tag anterior para o prefixo'
        } else {
            $changedFiles = Get-ChangedFilesSinceTag -Tag $latestTag
            $matchedFiles = Select-FilesByPrefixes -Files $changedFiles -Prefixes (Get-EspAppRelevantPathPrefixes -AppShortName $app.ShortName) -Excludes (Get-EspAppIrrelevantPathPrefixes)
            if ($matchedFiles.Count -gt 0) {
                $include = $true
                $reason = "arquivos alterados desde $latestTag"
            }
        }
        if (-not $include) { continue }
        if (-not $app.ManifestVersion) {
            throw "Versao do app da placa '$($app.ShortName)' nao encontrada."
        }
        if (-not (Test-SimpleSemVer -Version $app.ManifestVersion)) {
            throw "App da placa '$($app.ShortName)' com versao '$($app.ManifestVersion)' invalida."
        }
        $next = Get-NextPatchVersion -Version $app.ManifestVersion
        $plan += [pscustomobject]@{
            App         = $app
            Kind        = 'esp-app'
            Current     = $app.ManifestVersion
            Next        = $next
            TagPrefix   = $tagPrefix
            Tag         = "$tagPrefix$next"
            LatestTag   = $latestTag
            Reason      = $reason
            ChangedHint = @($matchedFiles | Select-Object -First 5)
        }
    }
    return $plan
}

# ---------- Comandos ----------

function Invoke-OsBuild {
    $envScript = Join-Path $script:CliRoot 'esp-idf-env.ps1'
    Assert-PathExists -Path $envScript -Description 'esp-idf-env.ps1'
    . $envScript
    $root = Get-IdfMirrorRoot
    $osMirror = Join-Path $root 'ribanense-esp'
    Invoke-RobocopyMirror -Source $script:OsRoot -Destination $osMirror
    Invoke-RobocopyMirror -Source $script:EspSdkRoot -Destination (Join-Path $root 'esp-sdk')
    Sync-IdfSdkconfigFromDefaults -ProjectDir $osMirror
    Invoke-IdfBuild -ProjectDir $osMirror -ExtraArgs @('build')
    Write-Ok "Build do OS concluido em $root\ribanense-esp\build."
}

function Invoke-OsFlash {
    $port = if ($script:RestArguments.Count -ge 1) { $script:RestArguments[0] } else { 'COM8' }
    Write-Warn2 "Recuperacao USB: grava bootloader, tabela de particoes e OS em $port."
    Write-Warn2 "Isto NAO e o caminho normal. Depois do gravar, a placa atualiza pelo GitHub."
    $mirror = Join-Path $script:CliRoot 'esp-idf-env.ps1'
    . $mirror
    $root = Get-IdfMirrorRoot
    $osMirror = Join-Path $root 'ribanense-esp'
    Invoke-OsBuild
    Invoke-IdfBuild -ProjectDir $osMirror -ExtraArgs @('-p', $port, 'flash')
    Write-Ok "Flash do OS enviado para $port."
}

function Invoke-SolutionBuild {
    Assert-PathExists -Path $script:SolutionPath -Description 'Solution'
    Stop-RibananseProcesses
    Write-Info "dotnet build $($script:SolutionPath | Split-Path -Leaf)"
    Invoke-DotNet -Arguments (@('build', $script:SolutionPath) + $script:RestArguments)
    Write-Ok "Build concluido."
}

function Invoke-AppRun {
    if ($script:RestArguments.Count -lt 1) {
        Invoke-LauncherRun
        return
    }
    $appName = $script:RestArguments[0]
    $projPath = Get-AppProjectPath -AppName $appName
    Stop-RibananseProcesses -NameFilters @("Ribanense.Solucoes.App.$appName")
    Write-Info "Compilando app '$appName'..."
    Invoke-DotNet -Arguments @('build', $projPath)
    $exePath = Get-AppOutputExe -AppName $appName
    Assert-PathExists -Path $exePath -Description "Executavel do app '$appName'"
    Write-Ok "Abrindo '$appName'..."
    Start-Process -FilePath $exePath
}

function Invoke-LauncherRun {
    Assert-PathExists -Path $script:LauncherProjectPath -Description 'Projeto do Launcher'
    Stop-RibananseProcesses -NameFilters @('Ribanense.Solucoes.Launcher')
    Write-Info "Compilando Launcher..."
    Invoke-DotNet -Arguments (@('build', $script:LauncherProjectPath) + $script:RestArguments)
    $exePath = Join-Path $script:ProjectRoot 'src\Ribanense.Solucoes.Launcher\bin\Debug\net10.0-windows\Ribanense.Solucoes.Launcher.exe'
    Assert-PathExists -Path $exePath -Description 'Executavel do Launcher'
    Write-Ok "Abrindo Launcher..."
    Start-Process -FilePath $exePath
}

function Invoke-PublishRun {
    $isLauncher = $script:RestArguments.Count -lt 1 -or ($script:RestArguments[0] -ieq 'Launcher')
    [string] $appShortName = $null
    if (-not $isLauncher) {
        $appShortName = $script:RestArguments[0]
        Get-AppProjectPath -AppName $appShortName | Out-Null
    }

    $slotName = if ($isLauncher) { 'Launcher' } else { $appShortName }
    $publishRoot = Join-Path $script:ProjectRoot 'artifacts\publish-run'
    $outDir = Join-Path $publishRoot "$slotName\out"

    if (Test-Path -LiteralPath $outDir) {
        Remove-Item -LiteralPath $outDir -Recurse -Force
    }
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null

    $projPath = if ($isLauncher) {
        $script:LauncherProjectPath
    } else {
        Get-AppProjectPath -AppName $appShortName
    }

    if ($isLauncher) {
        Stop-RibananseProcesses -NameFilters @('Ribanense.Solucoes.Launcher')
    } else {
        Stop-RibananseProcesses -NameFilters @("Ribanense.Solucoes.App.$appShortName")
    }

    Write-Info "dotnet publish Release win-x64 ($slotName) -> artifacts\publish-run\$slotName\out ..."
    Push-Location $script:ProjectRoot
    try {
        if ($isLauncher) {
            & dotnet publish $projPath `
                -c Release `
                -r win-x64 `
                --self-contained true `
                -p:PublishSingleFile=true `
                -p:IncludeNativeLibrariesForSelfExtract=true `
                -p:EnableCompressionInSingleFile=true `
                -p:PublishReadyToRun=true `
                -o $outDir
        } else {
            & dotnet publish $projPath `
                -c Release `
                -r win-x64 `
                --no-self-contained `
                -p:PublishReadyToRun=true `
                -o $outDir
        }
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
    finally {
        Pop-Location
    }

    $exeName = if ($isLauncher) {
        'Ribanense.Solucoes.Launcher.exe'
    } else {
        "Ribanense.Solucoes.App.$appShortName.exe"
    }
    $exePath = Join-Path $outDir $exeName
    Assert-PathExists -Path $exePath -Description "Executavel publicado ($slotName)"

    if ($isLauncher) {
        Write-Muted "Perfil single-file self-contained (igual ao asset de release). Pasta: $outDir"
    } else {
        Write-Muted "Mesmo perfil de rb publish (sem zip). Pasta: $outDir"
    }
    Write-Ok "Abrindo build Release publicado..."
    Start-Process -FilePath $exePath
}

function Invoke-SolutionTests {
    Assert-PathExists -Path $script:SolutionPath -Description 'Solution'
    Write-Info "dotnet test"
    Invoke-DotNet -Arguments (@('test', $script:SolutionPath) + $script:RestArguments)
    Write-Ok "Testes concluidos."
}

function Invoke-FullCheck {
    Stop-RibananseProcesses
    Write-Step "Build"
    Invoke-DotNet -Arguments @('build', $script:SolutionPath)
    Write-Step "Testes"
    Invoke-DotNet -Arguments @('test', $script:SolutionPath)
    Write-Ok "Check completo."
}

function Invoke-Clean {
    Stop-RibananseProcesses
    $patterns = @('bin', 'obj')
    $removed = 0
    $freed = 0L

    foreach ($pattern in $patterns) {
        foreach ($d in (Get-ChildItem -LiteralPath $script:ProjectRoot -Directory -Recurse -Filter $pattern -Force -ErrorAction SilentlyContinue)) {
            try {
                $size = (Get-ChildItem -LiteralPath $d.FullName -Recurse -File -Force -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum
                if ($size) { $freed += $size }
                Remove-Item -LiteralPath $d.FullName -Recurse -Force -ErrorAction Stop
                $removed++
            } catch {
                Write-Warn2 "Nao foi possivel remover: $($d.FullName) ($($_.Exception.Message))"
            }
        }
    }

    $artifacts = Join-Path $script:ProjectRoot 'artifacts'
    if (Test-Path -LiteralPath $artifacts) {
        try {
            $size = (Get-ChildItem -LiteralPath $artifacts -Recurse -File -Force -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum
            if ($size) { $freed += $size }
            Remove-Item -LiteralPath $artifacts -Recurse -Force
            $removed++
        } catch {
            Write-Warn2 "Nao foi possivel remover: $artifacts"
        }
    }

    $mb = [math]::Round($freed / 1MB, 1)
    Write-Ok "Limpeza concluida: $removed diretorio(s) removido(s), ~$mb MB liberados."
}

function Invoke-ListApps {
    $apps = Get-AllAppProjects
    if ($apps.Count -eq 0) {
        Write-Warn2 "Nenhum app encontrado em src\aplicativos\."
        return
    }
    Write-Host ""
    Write-Host ("{0,-14} {1,-10} {2,-10} {3}" -f 'ShortName', 'csproj', 'app.json', 'PublicName') -ForegroundColor Gray
    Write-Host ("{0,-14} {1,-10} {2,-10} {3}" -f '---------', '------', '--------', '----------') -ForegroundColor DarkGray
    foreach ($a in $apps) {
        $csprojVer = if ($a.CsprojVersion) { $a.CsprojVersion } else { '?' }
        $manifestVer = if ($a.ManifestVersion) { $a.ManifestVersion } else { '-' }
        $pub = if ($a.PublicName) { $a.PublicName } else { '(sem app.json)' }
        Write-Host ("{0,-14} {1,-10} {2,-10} {3}" -f $a.ShortName, $csprojVer, $manifestVer, $pub)
    }
    Write-Host ""
    Write-Muted "Total: $($apps.Count) app(s) Windows."

    $osVer = Get-OsVersion
    Write-Host ""
    Write-Host "RibanenseESP (OS)" -ForegroundColor Cyan
    if ($osVer) {
        Write-Host ("{0,-14} {1}" -f 'OS', $osVer)
    } else {
        Write-Warn2 "OS nao encontrado em firmware\ribanense-esp."
    }

    $espApps = @(Get-AllEspAppProjects)
    Write-Host ""
    Write-Host "Apps da placa" -ForegroundColor Cyan
    if ($espApps.Count -eq 0) {
        Write-Muted "Nenhum app em firmware\apps\."
    } else {
        Write-Host ("{0,-14} {1,-10} {2}" -f 'ShortName', 'app.json', 'PublicName') -ForegroundColor Gray
        foreach ($a in $espApps) {
            Write-Host ("{0,-14} {1,-10} {2}" -f $a.ShortName, $a.ManifestVersion, $a.PublicName)
        }
        Write-Muted "Total: $($espApps.Count) app(s) da placa."
    }
}

function Invoke-ShowVersions {
    $launcher = Get-LauncherVersion
    $sdk = Get-SdkVersion
    Write-Host ""
    Write-Host ("{0,-30} {1}" -f 'Launcher (Directory.Build.props)', $launcher) -ForegroundColor Cyan
    Write-Host ("{0,-30} {1}" -f 'PluginSDK.SdkVersion.Current', $sdk) -ForegroundColor Cyan

    $osVer = Get-OsVersion
    if ($osVer) {
        Write-Host ("{0,-30} {1}" -f 'RibanenseESP (OS)', $osVer) -ForegroundColor Cyan
    }

    $apps = Get-AllAppProjects
    if ($apps.Count -gt 0) {
        Write-Host ""
        Write-Host "Apps Windows:" -ForegroundColor Cyan
        foreach ($a in $apps) {
            $csprojVer = if ($a.CsprojVersion) { $a.CsprojVersion } else { '?' }
            $manifestVer = if ($a.ManifestVersion) { $a.ManifestVersion } else { '?' }
            $match = if ($csprojVer -eq $manifestVer) { '' } else { ' <!> csproj vs app.json divergente' }
            $line = "  {0,-14} csproj={1,-8} manifesto={2,-8}{3}" -f $a.ShortName, $csprojVer, $manifestVer, $match
            if ($match) {
                Write-Host $line -ForegroundColor Yellow
            } else {
                Write-Host $line
            }
        }
    }

    $espApps = @(Get-AllEspAppProjects)
    if ($espApps.Count -gt 0) {
        Write-Host ""
        Write-Host "Apps da placa:" -ForegroundColor Cyan
        foreach ($a in $espApps) {
            Write-Host ("  {0,-14} manifesto={1}" -f $a.ShortName, $a.ManifestVersion)
        }
    }
    Write-Host ""
}

function Invoke-DevLink {
    if ($script:RestArguments.Count -lt 1) {
        throw "Uso: rb devlink <App>. Exemplo: rb devlink Winget"
    }
    $appName = $script:RestArguments[0]
    $projPath = Get-AppProjectPath -AppName $appName
    $outDir = Get-AppOutputDir -AppName $appName
    $manifestPath = Join-Path (Split-Path -Parent $projPath) 'app.json'
    $dest = Join-Path $script:DevLinkRoot $appName

    Stop-RibananseProcesses -NameFilters @("Ribanense.Solucoes.App.$appName")

    Write-Info "Compilando '$appName'..."
    Invoke-DotNet -Arguments @('build', $projPath)

    if (-not (Test-Path -LiteralPath $outDir)) {
        throw "Diretorio de build nao encontrado: $outDir"
    }

    if (Test-Path -LiteralPath $dest) {
        Write-Muted "Substituindo devlink existente em $dest"
        Remove-Item -LiteralPath $dest -Recurse -Force
    }
    New-Item -ItemType Directory -Path $dest -Force | Out-Null

    Write-Info "Copiando binarios para $dest"
    Copy-Item -Path (Join-Path $outDir '*') -Destination $dest -Recurse -Force

    if (Test-Path -LiteralPath $manifestPath) {
        Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $dest 'app.json') -Force
        Write-Muted "app.json copiado."
    } else {
        Write-Warn2 "app.json nao encontrado em $manifestPath - Launcher nao vai reconhecer o app sem ele."
    }

    Write-Ok "Devlink concluido: '$appName' disponivel no Launcher."
    Write-Muted "Pasta: $dest"
    Write-Muted "Abra/reinicie o Launcher com 'rb run'."
}

function Invoke-DevUnlink {
    if ($script:RestArguments.Count -lt 1) {
        throw "Uso: rb unlink <App>. Exemplo: rb unlink Winget"
    }
    $appName = $script:RestArguments[0]
    $dest = Join-Path $script:DevLinkRoot $appName

    if (-not (Test-Path -LiteralPath $dest)) {
        Write-Warn2 "Nada para remover: $dest nao existe."
        return
    }

    Stop-RibananseProcesses -NameFilters @("Ribanense.Solucoes.App.$appName")
    Remove-Item -LiteralPath $dest -Recurse -Force
    Write-Ok "Devlink de '$appName' removido."
}

function Invoke-AppPublish {
    if ($script:RestArguments.Count -lt 1) {
        throw "Uso: rb publish <App|Launcher|all> [-Version <ver>] [-Yes] [--dry-run]. Exemplos: rb publish Winget -Version 0.2.0 ; rb publish Launcher -Version 0.1.0 ; rb publish all -Yes"
    }
    $targetName = $script:RestArguments[0]
    $remaining = @()
    if ($script:RestArguments.Count -gt 1) {
        $remaining = $script:RestArguments[1..($script:RestArguments.Count - 1)]
    }

    if ($targetName -ieq 'all') {
        Invoke-PublishAll -Arguments $remaining
        return
    }

    if ($targetName -ieq 'Launcher') {
        $publishScript = Join-Path $script:CliRoot 'publish-launcher.ps1'
        Assert-PathExists -Path $publishScript -Description 'publish-launcher.ps1'
        & $publishScript @remaining
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        Write-Ok "Pacote do Launcher gerado em artifacts\publish\Launcher\."
        return
    }

    if (Test-IsOsTarget -Name $targetName) {
        $publishScript = Join-Path $script:CliRoot 'publish-os.ps1'
        Assert-PathExists -Path $publishScript -Description 'publish-os.ps1'
        & $publishScript @remaining
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        Write-Ok "Pacote do OS gerado em artifacts\publish\RibanenseESP\."
        return
    }

    $espApps = @(Get-AllEspAppProjects)
    $espHit = @($espApps | Where-Object { $_.ShortName.Equals($targetName, [System.StringComparison]::OrdinalIgnoreCase) })
    if ($espHit.Count -eq 1) {
        $publishScript = Join-Path $script:CliRoot 'publish-esp-app.ps1'
        Assert-PathExists -Path $publishScript -Description 'publish-esp-app.ps1'
        & $publishScript -App $espHit[0].ShortName @remaining
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        Write-Ok "Pacote do app da placa '$($espHit[0].ShortName)' gerado em artifacts\publish\Esp$($espHit[0].ShortName)\."
        return
    }

    Get-AppProjectPath -AppName $targetName | Out-Null
    $publishScript = Join-Path $script:CliRoot 'publish-module.ps1'
    Assert-PathExists -Path $publishScript -Description 'publish-module.ps1'
    & $publishScript -App $targetName @remaining
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Write-Ok "Pacote de '$targetName' gerado em artifacts\publish\$targetName\."
}

function Invoke-PublishAllVersionCommit {
    param(
        [Parameter(Mandatory)] [object[]] $Plan,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [string[]] $VersionFiles
    )

    $files = @(
        foreach ($f in $VersionFiles) {
            if (-not [string]::IsNullOrWhiteSpace($f)) {
                [System.IO.Path]::GetFullPath($f)
            }
        }
    ) | Select-Object -Unique

    if ($files.Count -eq 0) {
        Write-Warn2 "Nenhum arquivo de versao para commitar."
        return
    }

    foreach ($f in $files) {
        if (-not (Test-Path -LiteralPath $f)) {
            throw "Arquivo de versao esperado nao encontrado para commit: $f"
        }
        & git add -- $f
        if ($LASTEXITCODE -ne 0) { throw "git add falhou para: $f" }
    }

    $stagedRaw = @(& git diff --cached --name-only)
    if ($LASTEXITCODE -ne 0) { throw "git diff --cached falhou." }
    $staged = @($stagedRaw | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    if ($staged.Count -eq 0) {
        Write-Warn2 "Nenhuma alteracao de versao para commitar (arquivos ja versionados?)."
        return
    }

    Write-Muted "Arquivos no commit de versao:"
    foreach ($s in $staged) { Write-Muted " + $s" }

    $summaryLines = @(
        foreach ($item in $Plan) {
            " - $($item.App.ShortName) $($item.Current) -> $($item.Next)"
        }
    )
    $commitTitle = 'chore(release): bump de versoes (publish all)'
    $commitBody = $summaryLines -join "`n"

    & git commit -m $commitTitle -m $commitBody
    if ($LASTEXITCODE -ne 0) { throw "git commit das versoes falhou." }

    Write-Info "Enviando commit de versoes para origin..."
    & git push origin HEAD
    if ($LASTEXITCODE -ne 0) { throw "git push origin HEAD falhou; publish all abortado antes das tags." }

    Write-Ok "Commit de versoes criado e enviado (tags apontarao para este commit)."
}

function Invoke-PublishAll {
    param([string[]] $Arguments = @())

    $autoYes = $false
    $dryRun = $false

    foreach ($arg in $Arguments) {
        $key = $arg.ToLowerInvariant()
        switch ($key) {
            '-yes'      { $autoYes = $true; continue }
            '--yes'     { $autoYes = $true; continue }
            '/yes'      { $autoYes = $true; continue }
            '-y'        { $autoYes = $true; continue }
            '--dry-run' { $dryRun = $true; continue }
            '--whatif'  { $dryRun = $true; continue }
            '-whatif'   { $dryRun = $true; continue }
            default {
                throw "Argumento desconhecido em publish all: '$arg'. Use -Yes e/ou --dry-run."
            }
        }
    }

    Assert-CommandAvailable 'git'
    Assert-CommandAvailable 'gh'

    Write-Info "Atualizando tags locais (git fetch --tags)..."
    Push-Location $script:ProjectRoot
    try {
        & git fetch --tags
        if ($LASTEXITCODE -ne 0) { throw "git fetch --tags falhou." }
    }
    finally {
        Pop-Location
    }

    & gh auth status *> $null
    if ($LASTEXITCODE -ne 0) {
        throw "gh auth status falhou. Execute 'gh auth login' antes do publish all."
    }

    Push-Location $script:ProjectRoot
    try {
        $dirtyBefore = @(& git status --porcelain)
        if ($dirtyBefore.Count -gt 0) {
            Write-Warn2 "Working tree ja possui alteracoes locais antes do publish all."
            foreach ($line in ($dirtyBefore | Select-Object -First 20)) {
                Write-Muted $line
            }
        }

        $apps = @(Get-AllAppProjects)
        $plan = @(Get-PublishAllCandidates -Apps $apps)
        $launcherPlan = Get-PublishAllLauncherPlanItem
        if ($launcherPlan) {
            $plan += $launcherPlan
        }
        $osPlan = Get-PublishAllOsPlanItem
        if ($osPlan) {
            $plan += $osPlan
        }
        $plan += @(Get-PublishAllEspAppCandidates)

        if ($plan.Count -eq 0) {
            if ($apps.Count -eq 0) {
                Write-Warn2 "Nenhum app encontrado em src\aplicativos\."
            }
            Write-Ok "Nenhum release necessario: nenhuma mudanca relevante desde as ultimas tags (apps, Launcher, OS)."
            return
        }

        Write-Step "Plano publish all"
        foreach ($item in $plan) {
            $kind = if ($item.Kind) { $item.Kind } else { 'win-app' }
            Write-Host ("{0,-14} {1} -> {2}  tag={3}  ({4})" -f $item.App.ShortName, $item.Current, $item.Next, $item.Tag, $kind)
            Write-Muted ("motivo: {0}" -f $item.Reason)
            if ($item.ChangedHint.Count -gt 0) {
                foreach ($f in $item.ChangedHint) {
                    Write-Muted (" - " + $f)
                }
            }
        }

        if ($dryRun) {
            Write-Host ""
            Write-Ok "Dry-run: nenhuma versao foi alterada e nenhum release foi publicado."
            return
        }

        if (-not $autoYes) {
            Write-Host ""
            $answer = Read-Host "Continuar e publicar os itens acima (Windows, OS e/ou apps da placa)? [s/N]"
            if ($answer -notin @('s', 'S', 'sim', 'SIM', 'y', 'Y', 'yes', 'YES')) {
                Write-Warn2 "Operacao cancelada pelo usuario."
                return
            }
        }

        Write-Step "Atualizando versoes (patch)"
        $versionFiles = @()
        foreach ($item in $plan) {
            $kind = if ($item.Kind) { $item.Kind } else { 'win-app' }
            switch ($kind) {
                'launcher' {
                    Set-LauncherVersion -Version $item.Next
                    $versionFiles += (Get-LauncherVersionState).BuildPropsPath
                }
                'os' {
                    Set-OsVersion -Version $item.Next
                    $versionFiles += $script:OsVersionHeader
                    if (Test-Path -LiteralPath $script:OsFirmwareJson) {
                        $versionFiles += $script:OsFirmwareJson
                    }
                }
                'esp-app' {
                    Set-EspAppVersion -App $item.App -Version $item.Next
                    $versionFiles += $item.App.ManifestPath
                    if (Test-Path -LiteralPath $item.App.CMakeListsPath) {
                        $versionFiles += $item.App.CMakeListsPath
                    }
                }
                default {
                    Set-AppVersion -App $item.App -Version $item.Next
                    $versionFiles += $item.App.ProjectPath
                    if ($item.App.HasManifest) {
                        $versionFiles += $item.App.ManifestPath
                    }
                }
            }
            Write-Ok "$($item.App.ShortName): $($item.Current) -> $($item.Next)"
        }

        $needsDotnet = @($plan | Where-Object { $_.Kind -in @('win-app', 'launcher') -or (-not $_.Kind) }).Count -gt 0
        if ($needsDotnet) {
            Write-Step "Validacao (.NET)"
            Invoke-FullCheck
        } else {
            Write-Muted "publish all so com OS/apps da placa: rb check (.NET) omitido."
        }

        Write-Step "Commit das versoes"
        Invoke-PublishAllVersionCommit -Plan $plan -VersionFiles $versionFiles

        Write-Step "Publicacao no GitHub"
        $releaseScript = Join-Path $script:CliRoot 'release.ps1'
        Assert-PathExists -Path $releaseScript -Description 'release.ps1'
        foreach ($item in $plan) {
            Write-Info "Publicando $($item.App.ShortName) v$($item.Next)..."
            & $releaseScript -App $item.App.ShortName -Version $item.Next
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        }

        Write-Step "Resumo"
        foreach ($item in $plan) {
            Write-Ok ("{0} publicado: {1} ({2})" -f $item.App.ShortName, $item.Next, $item.Tag)
        }
    }
    finally {
        Pop-Location
    }
}

function Invoke-AppRelease {
    if ($script:RestArguments.Count -lt 2) {
        throw "Uso: rb release <App|Launcher|OS> <semver>. Exemplos: rb release Winget 0.2.0 ; rb release Launcher 0.1.0 ; rb release OS 0.0.3"
    }
    $appName = $script:RestArguments[0]
    $version = $script:RestArguments[1]
    $isOs = Test-IsOsTarget -Name $appName
    $isEsp = @(Get-AllEspAppProjects | Where-Object { $_.ShortName.Equals($appName, [System.StringComparison]::OrdinalIgnoreCase) }).Count -eq 1
    if ($appName -ine 'Launcher' -and -not $isOs -and -not $isEsp) {
        Get-AppProjectPath -AppName $appName | Out-Null
    }
    if ($isOs) { $appName = 'OS' }
    if ($isEsp) { $appName = Resolve-EspAppShortName -AppInput $appName }

    $releaseScript = Join-Path $script:CliRoot 'release.ps1'
    Assert-PathExists -Path $releaseScript -Description 'release.ps1'
    & $releaseScript -App $appName -Version $version
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Write-Ok "Release '$appName' $version publicado."
}

function Invoke-Logs {
    # rb logs                    -> launcher, ultimas 100 entradas
    # rb logs Winget             -> app Winget, ultimas 100 entradas
    # rb logs Winget 200         -> app Winget, ultimas 200 entradas
    # rb logs 50                 -> launcher, ultimas 50 entradas
    $count = 100
    $target = 'launcher'

    if ($script:RestArguments.Count -ge 1) {
        $first = $script:RestArguments[0]
        $parsedInt = 0
        if ([int]::TryParse($first, [ref] $parsedInt) -and $parsedInt -gt 0) {
            $count = $parsedInt
        } else {
            $target = $first
            if ($script:RestArguments.Count -ge 2) {
                if ([int]::TryParse($script:RestArguments[1], [ref] $parsedInt) -and $parsedInt -gt 0) {
                    $count = $parsedInt
                }
            }
        }
    }

    if ($target -ieq 'launcher') {
        Assert-PathExists -Path $script:LauncherProjectPath -Description 'Projeto do Launcher'
        Write-Info "Compilando Launcher (se necessario) para ler $count ultima(s) entrada(s)..."
        Invoke-DotNet -Arguments @('build', $script:LauncherProjectPath, '-v', 'quiet', '--nologo')
        $exePath = Join-Path $script:ProjectRoot 'src\Ribanense.Solucoes.Launcher\bin\Debug\net10.0-windows\Ribanense.Solucoes.Launcher.exe'
        Assert-PathExists -Path $exePath -Description 'Executavel do Launcher'
        Write-Host ""
        & $exePath --logs $count
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        return
    }

    $projPath = Get-AppProjectPath -AppName $target
    Write-Info "Compilando '$target' (se necessario) para ler $count ultima(s) entrada(s)..."
    Invoke-DotNet -Arguments @('build', $projPath, '-v', 'quiet', '--nologo')
    $exePath = Get-AppOutputExe -AppName $target
    Assert-PathExists -Path $exePath -Description "Executavel do app '$target'"
    Write-Host ""
    & $exePath --logs $count
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

function Invoke-CrashLog {
    $crashPath = Join-Path $script:LauncherDataRoot 'crash.log'
    $oldPath = Join-Path $script:LauncherDataRoot 'crash.old.log'

    $anything = $false
    if (Test-Path -LiteralPath $oldPath) {
        Write-Info "--- crash.old.log ---"
        Get-Content -LiteralPath $oldPath -Tail 200
        $anything = $true
        Write-Host ""
    }
    if (Test-Path -LiteralPath $crashPath) {
        Write-Info "--- crash.log ($crashPath) ---"
        Get-Content -LiteralPath $crashPath -Tail 200
        $anything = $true
    }
    if (-not $anything) {
        Write-Ok "Sem crashes registrados (esperado em primeiro uso)."
        Write-Muted "Caminho monitorado: $crashPath"
    }
}

function Invoke-CrashLogClear {
    $crashPath = Join-Path $script:LauncherDataRoot 'crash.log'
    $oldPath = Join-Path $script:LauncherDataRoot 'crash.old.log'
    $removed = 0
    foreach ($p in @($crashPath, $oldPath)) {
        if (Test-Path -LiteralPath $p) {
            Remove-Item -LiteralPath $p -Force
            $removed++
        }
    }
    Write-Ok "Crash logs limpos ($removed arquivo(s) removido(s))."
}

function Invoke-InstallRbCommand {
    $scope = 'User'
    if ($script:RestArguments.Count -ge 1) {
        $candidate = $script:RestArguments[0].ToLowerInvariant()
        switch ($candidate) {
            'user'    { $scope = 'User' }
            'session' { $scope = 'Session' }
            default   { throw "Uso: rb install [user|session]. Exemplo: rb install session" }
        }
    }

    $installScript = Join-Path $script:CliRoot 'install-rb-command.ps1'
    Assert-PathExists -Path $installScript -Description 'install-rb-command.ps1'

    & $installScript -Scope $scope
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Write-Ok "Instalacao do comando global 'rb' concluida (scope: $scope)."
}

# ---------- Tabela de comandos ----------

$script:Commands = @(
    [pscustomobject]@{ Verb = 'install'; Aliases = @('setup');             Handler = 'Invoke-InstallRbCommand'; Usage = 'rb install [user|session]';           Help = 'Instala o comando global rb (sem .\). user = persistente; session = apenas terminal atual.' },
    [pscustomobject]@{ Verb = 'build';   Aliases = @('compilar');          Handler = 'Invoke-SolutionBuild'; Usage = 'rb build [args]';                    Help = 'Compila a solution inteira.' },
    [pscustomobject]@{ Verb = 'run';     Aliases = @('rodar');             Handler = 'Invoke-AppRun';        Usage = 'rb run [App]';                       Help = 'Compila Debug e abre o Launcher ou o app (fluxo dev rapido).' },
    [pscustomobject]@{ Verb = 'publish-run'; Aliases = @('prun');          Handler = 'Invoke-PublishRun';   Usage = 'rb publish-run [App|Launcher]';    Help = 'Publica Release win-x64 (igual ao pacote de release) em artifacts\publish-run e abre o .exe para testar antes de rb release.' },
    [pscustomobject]@{ Verb = 'test';    Aliases = @('testar');            Handler = 'Invoke-SolutionTests'; Usage = 'rb test [args]';                     Help = 'Executa dotnet test.' },
    [pscustomobject]@{ Verb = 'check';   Aliases = @('validar');           Handler = 'Invoke-FullCheck';     Usage = 'rb check';                           Help = 'Build + test em sequencia.' },
    [pscustomobject]@{ Verb = 'clean';   Aliases = @('limpar');            Handler = 'Invoke-Clean';         Usage = 'rb clean';                           Help = 'Remove bin/, obj/, artifacts/ e encerra processos Ribanense do repo.' },
    [pscustomobject]@{ Verb = 'list';    Aliases = @('apps', 'ls');        Handler = 'Invoke-ListApps';      Usage = 'rb list';                            Help = 'Lista apps Windows, o OS RibanenseESP e os apps da placa.' },
    [pscustomobject]@{ Verb = 'version'; Aliases = @('versao');            Handler = 'Invoke-ShowVersions';  Usage = 'rb version';                         Help = 'Mostra versoes de Launcher, SDK, OS e cada app.' },
    [pscustomobject]@{ Verb = 'devlink'; Aliases = @('link');              Handler = 'Invoke-DevLink';       Usage = 'rb devlink <App>';                   Help = 'Compila um app e copia para %LOCALAPPDATA%\Ribanense Solucoes\aplicativos\ para o Launcher ver como instalado.' },
    [pscustomobject]@{ Verb = 'unlink';  Aliases = @('devunlink');         Handler = 'Invoke-DevUnlink';     Usage = 'rb unlink <App>';                    Help = 'Remove o devlink de um app.' },
    [pscustomobject]@{ Verb = 'publish';      Aliases = @('empacotar');         Handler = 'Invoke-AppPublish';     Usage = 'rb publish <App|Launcher|all> [...]'; Help = 'Empacota App/Launcher ou executa publish all (bump patch + check + releases do Launcher e/ou apps com mudancas desde a ultima tag).' },
    [pscustomobject]@{ Verb = 'release';      Aliases = @();                    Handler = 'Invoke-AppRelease';     Usage = 'rb release <App|Launcher> <semver>';   Help = 'Cria tag git e publica GitHub Release (requer gh CLI).' },
    [pscustomobject]@{ Verb = 'logs';         Aliases = @('log');               Handler = 'Invoke-Logs';           Usage = 'rb logs [App] [N]';                 Help = 'Imprime as ultimas N (default 100) entradas do vault (Launcher ou app). Usa copia temporaria, nao conflita com processo rodando.' },
    [pscustomobject]@{ Verb = 'crashlog';     Aliases = @('crash');             Handler = 'Invoke-CrashLog';       Usage = 'rb crashlog';                       Help = 'Mostra o crash.log (texto plano) com as ultimas 200 linhas. Inclui crash.old.log rotacionado se existir.' },
    [pscustomobject]@{ Verb = 'crashlog-clear';Aliases= @('crash-clear');       Handler = 'Invoke-CrashLogClear';  Usage = 'rb crashlog-clear';                 Help = 'Remove crash.log e crash.old.log.' },
    [pscustomobject]@{ Verb = 'help';         Aliases = @('?', '-h', '--help'); Handler = 'Show-RibananseCliHelp'; Usage = 'rb help';                           Help = 'Esta ajuda.' }
)

function Normalize-GroupToken {
    param([Parameter(Mandatory)] [string] $Token)
    $group = $Token.ToLowerInvariant()
    switch ($group) {
        'app'      { return 'app' }
        'module'   { return 'app' }
        'launcher' { return 'launcher' }
        'solution' { return 'solution' }
        'sln'      { return 'solution' }
        'repo'     { return 'solution' }
        'os'       { return 'os' }
        'esp'      { return 'os' }
        'ribanenseesp' { return 'os' }
        default    { return $null }
    }
}

function Show-GroupHelp {
    param([Parameter(Mandatory)] [string] $GroupName)

    $group = Normalize-GroupToken -Token $GroupName
    if ($null -eq $group) {
        throw "Grupo desconhecido: '$GroupName'. Use: app, launcher, solution, os."
    }

    Write-Host ""
    switch ($group) {
        'app' {
            Write-Host "Grupo: app" -ForegroundColor Cyan
            Write-Host "-----------" -ForegroundColor DarkCyan
            Write-Host "Uso base: rb app <acao> <App> [args]"
            Write-Host ""
            Write-Host "Acoes:" -ForegroundColor Gray
            Write-Host "  run | executar | abrir"
            Write-Host "  publish-run | prun"
            Write-Host "  publish | empacotar | pack"
            Write-Host "  release | soltar"
            Write-Host "  devlink | link"
            Write-Host "  unlink | devunlink"
            Write-Host "  logs | log"
            Write-Host ""
            Write-Host "Exemplos:" -ForegroundColor Gray
            Write-Host "  .\rb.cmd app run winget"
            Write-Host "  .\rb.cmd app publish-run chocolatey"
            Write-Host "  .\rb.cmd app publish winget -Version 0.2.0"
            Write-Host "  .\rb.cmd app publish all --dry-run"
            Write-Host "  .\rb.cmd app publish all -Yes"
            Write-Host "  .\rb.cmd app release winget 0.2.0"
            Write-Host "  .\rb.cmd app logs winget 50"
        }
        'launcher' {
            Write-Host "Grupo: launcher" -ForegroundColor Cyan
            Write-Host "----------------" -ForegroundColor DarkCyan
            Write-Host "Uso base: rb launcher <acao> [args]"
            Write-Host ""
            Write-Host "Acoes:" -ForegroundColor Gray
            Write-Host "  run | executar | abrir"
            Write-Host "  publish-run | prun"
            Write-Host "  publish | empacotar | pack"
            Write-Host "  release | soltar"
            Write-Host "  logs | log"
            Write-Host ""
            Write-Host "Exemplos:" -ForegroundColor Gray
            Write-Host "  .\rb.cmd launcher run"
            Write-Host "  .\rb.cmd launcher publish-run"
            Write-Host "  .\rb.cmd launcher publish -Version 0.1.0"
            Write-Host "  .\rb.cmd launcher release 0.1.0"
            Write-Host "  .\rb.cmd launcher logs 100"
        }
        'solution' {
            Write-Host "Grupo: solution" -ForegroundColor Cyan
            Write-Host "----------------" -ForegroundColor DarkCyan
            Write-Host "Uso base: rb solution <acao> [args]"
            Write-Host ""
            Write-Host "Acoes:" -ForegroundColor Gray
            Write-Host "  build | compilar"
            Write-Host "  test | testar"
            Write-Host "  check | validar"
            Write-Host "  clean | limpar"
            Write-Host "  list | apps | ls"
            Write-Host "  version | versao"
            Write-Host ""
            Write-Host "Exemplos:" -ForegroundColor Gray
            Write-Host "  .\rb.cmd solution build"
            Write-Host "  .\rb.cmd solution test"
            Write-Host "  .\rb.cmd solution check"
            Write-Host "  .\rb.cmd solution clean"
            Write-Host "  .\rb.cmd solution list"
        }
        'os' {
            Write-Host "Grupo: os (RibanenseESP)" -ForegroundColor Cyan
            Write-Host "------------------------" -ForegroundColor DarkCyan
            Write-Host "Uso base: rb os <acao> [args]"
            Write-Host ""
            Write-Host "Acoes:" -ForegroundColor Gray
            Write-Host "  build | compilar"
            Write-Host "  publish | empacotar | pack"
            Write-Host "  release | soltar"
            Write-Host "  flash | gravar"
            Write-Host "  version | versao"
            Write-Host "  app publish <App>"
            Write-Host "  app release <App> <semver>"
            Write-Host ""
            Write-Host "Exemplos:" -ForegroundColor Gray
            Write-Host "  .\rb.cmd os build"
            Write-Host "  .\rb.cmd os publish -Version 0.0.3"
            Write-Host "  .\rb.cmd os release 0.0.3"
            Write-Host "  .\rb.cmd os flash COM8          # 1o gravar OU recuperacao; OTA normal e GitHub"
            Write-Host "  .\rb.cmd os app publish Sobre -Version 0.1.0"
            Write-Host "  .\rb.cmd os app release Sobre 0.1.0"
            Write-Host "  .\rb.cmd esp version"
        }
    }
    Write-Host ""
}

function Show-RibananseCliHelp {
    if ($script:RestArguments.Count -ge 1) {
        $group = Normalize-GroupToken -Token $script:RestArguments[0]
        if ($null -ne $group) {
            Show-GroupHelp -GroupName $group
            return
        }
    }

    Write-Host ""
    Write-Host "Ribanense Solucoes — CLI de desenvolvimento" -ForegroundColor Cyan
    Write-Host "============================================" -ForegroundColor DarkCyan
    Write-Host ""
    Write-Host "Sintaxe por dominio (estilo pc):" -ForegroundColor Gray
    Write-Host "  rb app <acao> <App> [args]"
    Write-Host "  rb launcher <acao> [args]"
    Write-Host "  rb os <acao> [args]"
    Write-Host "  rb solution <acao> [args]"
    Write-Host "  rb help app|launcher|os|solution"
    Write-Host ""
    Write-Host "Comandos:" -ForegroundColor Gray
    foreach ($c in $script:Commands) {
        $aliasStr = if ($c.Aliases.Count -gt 0) { " ({0})" -f ($c.Aliases -join ', ') } else { '' }
        $verbLine = "  {0}{1}" -f $c.Verb, $aliasStr
        Write-Host $verbLine -ForegroundColor Yellow
        Write-Host ("      Uso    : {0}" -f $c.Usage) -ForegroundColor Gray
        Write-Host ("      Descr. : {0}" -f $c.Help) -ForegroundColor DarkGray
    }
    Write-Host ""
    Write-Host "Exemplos:" -ForegroundColor Gray
    Write-Host "  .\rb.cmd install                # instala rb global no PATH do usuario"
    Write-Host "  .\rb.cmd install session        # instala so na sessao atual"
    Write-Host "  .\rb.cmd list"
    Write-Host "  .\rb.cmd run"
    Write-Host "  .\rb.cmd run Winget"
    Write-Host "  .\rb.cmd publish-run"
    Write-Host "  .\rb.cmd publish-run Winget"
    Write-Host "  .\rb.cmd devlink Winget"
    Write-Host "  .\rb.cmd check"
    Write-Host "  .\rb.cmd logs                    # launcher, ultimas 100"
    Write-Host "  .\rb.cmd logs Winget 50          # app Winget, ultimas 50"
    Write-Host "  .\rb.cmd crashlog                # texto plano do crash.log"
    Write-Host "  .\rb.cmd publish Winget -Version 0.2.0"
    Write-Host "  .\rb.cmd publish Launcher -Version 0.1.0"
    Write-Host "  .\rb.cmd publish all --dry-run"
    Write-Host "  .\rb.cmd publish all -Yes"
    Write-Host "  .\rb.cmd release Winget 0.2.0"
    Write-Host "  .\rb.cmd release Launcher 0.1.0"
    Write-Host ""
}

function Find-MatchingCommand {
    param([Parameter(Mandatory)] [string] $VerbOrAlias)
    $q = $VerbOrAlias.ToLowerInvariant()
    foreach ($c in $script:Commands) {
        if ($c.Verb -eq $q) { return $c }
        foreach ($a in $c.Aliases) {
            if ($a -eq $q) { return $c }
        }
    }
    return $null
}

function Show-CommandSuggestions {
    param([Parameter(Mandatory)] [string] $VerbOrAlias)
    $q = $VerbOrAlias.ToLowerInvariant()
    $candidates = @()
    foreach ($c in $script:Commands) {
        $all = @($c.Verb) + $c.Aliases
        foreach ($name in $all) {
            if ($name.StartsWith($q) -or $q.StartsWith($name) -or $name.Contains($q)) {
                $candidates += $c.Verb
                break
            }
        }
    }
    $candidates = $candidates | Select-Object -Unique
    if ($candidates.Count -gt 0 -and $candidates.Count -lt $script:Commands.Count) {
        Write-Host ""
        Write-Muted "Quis dizer: $($candidates -join ', ')?"
    }
}

function TryInvoke-GroupedCommand {
    param([Parameter(Mandatory)] [string] $GroupToken)

    $group = Normalize-GroupToken -Token $GroupToken
    if ($null -eq $group) {
        return $false
    }

    if ($script:RestArguments.Count -lt 1 -or $script:RestArguments[0].ToLowerInvariant() -in @('help', '?', '-h', '--help')) {
        Show-GroupHelp -GroupName $group
        return $true
    }

    $actionInput = $script:RestArguments[0].ToLowerInvariant()
    $actionArgs = @()
    if ($script:RestArguments.Count -gt 1) {
        $actionArgs = $script:RestArguments[1..($script:RestArguments.Count - 1)]
    }

    switch ($group) {
        'app' {
            $action = switch ($actionInput) {
                { $_ -in @('run', 'rodar', 'executar', 'abrir') } { 'run'; break }
                { $_ -in @('publish-run', 'prun') }                { 'publish-run'; break }
                { $_ -in @('publish', 'empacotar', 'pack') }       { 'publish'; break }
                { $_ -in @('release', 'soltar') }                  { 'release'; break }
                { $_ -in @('devlink', 'link') }                    { 'devlink'; break }
                { $_ -in @('unlink', 'devunlink') }                { 'unlink'; break }
                { $_ -in @('logs', 'log') }                        { 'logs'; break }
                default { $null }
            }
            if ($null -eq $action) {
                throw "Acao desconhecida para grupo app: '$actionInput'. Use 'rb help app'."
            }

            if ($actionArgs.Count -lt 1) {
                throw "Uso: rb app $action <App> [...]. Exemplo: rb app run winget"
            }
            $tail = @()
            if ($actionArgs.Count -gt 1) {
                $tail = $actionArgs[1..($actionArgs.Count - 1)]
            }

            switch ($action) {
                'run' {
                    $appName = Resolve-AppShortName -AppInput $actionArgs[0]
                    $script:RestArguments = @($appName)
                    Invoke-AppRun
                    return $true
                }
                'publish-run' {
                    $appName = Resolve-AppShortName -AppInput $actionArgs[0]
                    $script:RestArguments = @($appName)
                    Invoke-PublishRun
                    return $true
                }
                'publish' {
                    if ($actionArgs[0].ToLowerInvariant() -eq 'all') {
                        $script:RestArguments = @('all') + $tail
                        Invoke-AppPublish
                        return $true
                    }
                    $appName = Resolve-AppShortName -AppInput $actionArgs[0]
                    $script:RestArguments = @($appName) + $tail
                    Invoke-AppPublish
                    return $true
                }
                'release' {
                    $appName = Resolve-AppShortName -AppInput $actionArgs[0]
                    $script:RestArguments = @($appName) + $tail
                    Invoke-AppRelease
                    return $true
                }
                'devlink' {
                    $appName = Resolve-AppShortName -AppInput $actionArgs[0]
                    $script:RestArguments = @($appName)
                    Invoke-DevLink
                    return $true
                }
                'unlink' {
                    $appName = Resolve-AppShortName -AppInput $actionArgs[0]
                    $script:RestArguments = @($appName)
                    Invoke-DevUnlink
                    return $true
                }
                'logs' {
                    $appName = Resolve-AppShortName -AppInput $actionArgs[0]
                    $script:RestArguments = @($appName) + $tail
                    Invoke-Logs
                    return $true
                }
            }
        }
        'launcher' {
            $action = switch ($actionInput) {
                { $_ -in @('run', 'rodar', 'executar', 'abrir') } { 'run'; break }
                { $_ -in @('publish-run', 'prun') }                { 'publish-run'; break }
                { $_ -in @('publish', 'empacotar', 'pack') }       { 'publish'; break }
                { $_ -in @('release', 'soltar') }                  { 'release'; break }
                { $_ -in @('logs', 'log') }                        { 'logs'; break }
                default { $null }
            }
            if ($null -eq $action) {
                throw "Acao desconhecida para grupo launcher: '$actionInput'. Use 'rb help launcher'."
            }

            switch ($action) {
                'run' {
                    $script:RestArguments = @()
                    Invoke-LauncherRun
                    return $true
                }
                'publish-run' {
                    $script:RestArguments = @('Launcher')
                    Invoke-PublishRun
                    return $true
                }
                'publish' {
                    $script:RestArguments = @('Launcher') + $actionArgs
                    Invoke-AppPublish
                    return $true
                }
                'release' {
                    $script:RestArguments = @('Launcher') + $actionArgs
                    Invoke-AppRelease
                    return $true
                }
                'logs' {
                    $script:RestArguments = $actionArgs
                    Invoke-Logs
                    return $true
                }
            }
        }
        'os' {
            if ($actionInput -eq 'app') {
                if ($actionArgs.Count -lt 2) {
                    throw "Uso: rb os app publish|release <App> [...]."
                }
                $sub = $actionArgs[0].ToLowerInvariant()
                $tail = @()
                if ($actionArgs.Count -gt 1) {
                    $tail = $actionArgs[1..($actionArgs.Count - 1)]
                }
                if ($sub -in @('publish', 'empacotar', 'pack')) {
                    $script:RestArguments = $tail
                    Invoke-AppPublish
                    return $true
                }
                if ($sub -in @('release', 'soltar')) {
                    $script:RestArguments = $tail
                    Invoke-AppRelease
                    return $true
                }
                throw "Acao desconhecida para rb os app: '$sub'."
            }

            $action = switch ($actionInput) {
                { $_ -in @('build', 'compilar') } { 'build'; break }
                { $_ -in @('publish', 'empacotar', 'pack') } { 'publish'; break }
                { $_ -in @('release', 'soltar') } { 'release'; break }
                { $_ -in @('flash', 'gravar') } { 'flash'; break }
                { $_ -in @('version', 'versao') } { 'version'; break }
                default { $null }
            }
            if ($null -eq $action) {
                throw "Acao desconhecida para grupo os: '$actionInput'. Use 'rb help os'."
            }
            switch ($action) {
                'build' {
                    Invoke-OsBuild
                    return $true
                }
                'publish' {
                    $script:RestArguments = @('OS') + $actionArgs
                    Invoke-AppPublish
                    return $true
                }
                'release' {
                    $script:RestArguments = @('OS') + $actionArgs
                    Invoke-AppRelease
                    return $true
                }
                'flash' {
                    $script:RestArguments = $actionArgs
                    Invoke-OsFlash
                    return $true
                }
                'version' {
                    Invoke-ShowVersions
                    return $true
                }
            }
        }
        'solution' {
            $action = switch ($actionInput) {
                { $_ -in @('build', 'compilar') } { 'build'; break }
                { $_ -in @('test', 'testar') }    { 'test'; break }
                { $_ -in @('check', 'validar') }  { 'check'; break }
                { $_ -in @('clean', 'limpar') }   { 'clean'; break }
                { $_ -in @('list', 'apps', 'ls') } { 'list'; break }
                { $_ -in @('version', 'versao') } { 'version'; break }
                default { $null }
            }
            if ($null -eq $action) {
                throw "Acao desconhecida para grupo solution: '$actionInput'. Use 'rb help solution'."
            }

            $script:RestArguments = $actionArgs
            switch ($action) {
                'build'   { Invoke-SolutionBuild; return $true }
                'test'    { Invoke-SolutionTests; return $true }
                'check'   { Invoke-FullCheck; return $true }
                'clean'   { Invoke-Clean; return $true }
                'list'    { Invoke-ListApps; return $true }
                'version' { Invoke-ShowVersions; return $true }
            }
        }
    }

    return $false
}

# ---------- Dispatch ----------

try {
    if (TryInvoke-GroupedCommand -GroupToken $Command) {
        exit 0
    }

    $match = Find-MatchingCommand -VerbOrAlias $Command
    if ($null -eq $match) {
        Write-Err "Comando desconhecido: '$Command'"
        Show-CommandSuggestions -VerbOrAlias $Command
        Write-Host ""
        Show-RibananseCliHelp
        exit 1
    }

    & $match.Handler
}
catch {
    Write-Host ""
    Write-Err $_.Exception.Message
    if ($env:RIBANENSE_CLI_TRACE -eq '1') {
        Write-Host ""
        Write-Muted ($_ | Out-String).Trim()
    } else {
        Write-Muted "Dica: defina RIBANENSE_CLI_TRACE=1 para ver o stack trace."
    }
    exit 1
}
