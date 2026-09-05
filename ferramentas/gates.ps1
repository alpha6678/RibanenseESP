#Requires -Version 5.1
<#
.SYNOPSIS
  Gates de saude do RibanenseESP.

  A pergunta que estes testes respondem e uma so: "esta versao ainda consegue
  se atualizar sozinha pelo GitHub?". O OTA baixa por TLS e o GitHub manda
  records de 16 KB que o mbedtls precisa receber num bloco contiguo de
  16749 B. Quem gasta esse bloco nao e o OTA: e todo o resto do firmware.
  Por isso o gate mede memoria estatica e configuracao, nao o codigo de OTA.

  Nenhum teste aqui precisa de placa. O ensaio em hardware fica no
  Invoke-OtaRehearsal, que roda contra uma placa ja gravada.
#>

# Bloco contiguo que o record TLS do GitHub exige (esp-tls/mbedtls).
$script:TlsRecordBytes = 16749

function Get-GateBaselinePath {
    param([Parameter(Mandatory)] [string] $ProjectRoot)
    return (Join-Path $ProjectRoot 'ferramentas\baseline-memoria.json')
}

<#
  Menor particao de app na tabela que o build acabou de gerar, lida do
  partition-table.bin -- o mesmo blob que vai para a flash. Cada entrada tem
  32 bytes: magic 0xAA 0x50, tipo, subtipo, offset, tamanho, rotulo, flags.
  Tipo 0 e app. Devolve 0 se nao achar nenhuma.
#>
function Get-SmallestAppPartition {
    param([Parameter(Mandatory)] [string] $BuildDir)
    $tbl = Join-Path $BuildDir 'partition_table\partition-table.bin'
    if (-not (Test-Path -LiteralPath $tbl)) { return 0 }
    $bytes = [IO.File]::ReadAllBytes($tbl)
    $menor = 0
    for ($i = 0; $i + 32 -le $bytes.Length; $i += 32) {
        if ($bytes[$i] -ne 0xAA -or $bytes[$i + 1] -ne 0x50) { break }
        if ($bytes[$i + 2] -ne 0) { continue }
        $sz = [BitConverter]::ToUInt32($bytes, $i + 8)
        if ($menor -eq 0 -or $sz -lt $menor) { $menor = $sz }
    }
    return $menor
}

function Get-GateBaseline {
    param([Parameter(Mandatory)] [string] $ProjectRoot)
    $path = Get-GateBaselinePath -ProjectRoot $ProjectRoot
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    return (Get-Content -LiteralPath $path -Raw | ConvertFrom-Json)
}

<#
  Le o mapa de link e devolve as secoes. used_dram e a DRAM que o firmware
  ocupa antes de rodar uma linha: cada byte aqui e um byte a menos de heap
  para o TLS.
#>
function Get-LinkSizes {
    param(
        [Parameter(Mandatory)] [string] $MapPath,
        [Parameter(Mandatory)] [string] $PythonExe
    )
    if (-not (Test-Path -LiteralPath $MapPath)) {
        throw "Mapa de link nao encontrado: $MapPath. Rode 'rbesp build' antes."
    }
    $raw = & $PythonExe -m esp_idf_size --format json $MapPath 2>&1
    $text = (@($raw) | ForEach-Object { [string] $_ }) -join "`n"
    $start = $text.IndexOf('{')
    if ($start -lt 0) {
        throw "esp_idf_size nao devolveu JSON para $MapPath"
    }
    return ($text.Substring($start) | ConvertFrom-Json)
}

<# sdkconfig gerado -> tabela chave/valor. Chave ausente conta como 'n'. #>
function Get-SdkconfigMap {
    param([Parameter(Mandatory)] [string] $Path)
    $map = @{}
    if (-not (Test-Path -LiteralPath $Path)) { return $map }
    foreach ($line in (Get-Content -LiteralPath $Path)) {
        if ($line -match '^(CONFIG_[A-Z0-9_]+)=(.*)$') {
            $map[$Matches[1]] = $Matches[2].Trim('"')
        }
    }
    return $map
}

<#
  Placar. Cada gate diz o que quebra se ele for ignorado: a mensagem existe
  para quem for ler daqui a seis meses sem lembrar do bug original.
#>
function New-GateBoard {
    return [pscustomobject]@{
        Rows   = New-Object System.Collections.ArrayList
        Failed = 0
    }
}

function Add-GateRow {
    param(
        [Parameter(Mandatory)] $Board,
        [Parameter(Mandatory)] [bool] $Ok,
        [Parameter(Mandatory)] [string] $Name,
        [string] $Detail = '',
        [string] $Why = '',
        [switch] $WarnOnly
    )
    $tag = if ($Ok) { '[OK]' } elseif ($WarnOnly) { '[..]' } else { '[!!]' }
    $color = if ($Ok) { 'Green' } elseif ($WarnOnly) { 'Cyan' } else { 'Yellow' }
    $line = "$tag $Name"
    if ($Detail) { $line += "  $Detail" }
    Write-Host $line -ForegroundColor $color
    if (-not $Ok -and $Why) { Write-Host "     $Why" -ForegroundColor DarkGray }
    if (-not $Ok -and -not $WarnOnly) { $Board.Failed++ }
    $null = $Board.Rows.Add([pscustomobject]@{ Ok = $Ok; Name = $Name; Detail = $Detail })
    return
}

<#
  Gates estaticos: rodam sobre o build do espelho, sem placa.
  -UpdateBaseline grava os numeros medidos como novo teto (use depois de uma
  mudanca que aumenta a memoria de proposito e ja foi validada em hardware).
#>
function Invoke-HealthGates {
    param(
        [Parameter(Mandatory)] [string] $ProjectRoot,
        [Parameter(Mandatory)] [string] $MirrorRoot,
        [Parameter(Mandatory)] [string] $PythonExe,
        [switch] $UpdateBaseline
    )

    $build = Join-Path $MirrorRoot 'ribanense-esp\build'
    $map = Join-Path $build 'ribanense_esp.map'
    $bin = Join-Path $build 'ribanense_esp.bin'
    $cfgPath = Join-Path $MirrorRoot 'ribanense-esp\sdkconfig'

    if (-not (Test-Path -LiteralPath $map)) {
        throw "Sem build no espelho ($build). Rode 'rbesp build' antes de 'rbesp check'."
    }

    $board = New-GateBoard
    $base = Get-GateBaseline -ProjectRoot $ProjectRoot
    Write-Host "Gates de saude — a pergunta e se esta versao ainda se atualiza sozinha." -ForegroundColor Cyan
    Write-Host ""

    # --- 1. DRAM estatica ------------------------------------------------
    $sizes = Get-LinkSizes -MapPath $map -PythonExe $PythonExe
    $usedDram = [int] $sizes.used_dram
    $dramTotal = [int] $sizes.dram_total
    if ($base -and $base.usedDramMax) {
        $teto = [int] $base.usedDramMax
        $folga = $teto - $usedDram
        # Com -UpdateBaseline o usuario ja esta dizendo "aceito este valor":
        # reprovar por ele seria contraditorio.
        Add-GateRow -Board $board -Ok ($usedDram -le $teto) -WarnOnly:$UpdateBaseline `
            -Name 'DRAM estatica' `
            -Detail "$usedDram B usados, teto $teto B (folga $folga B)" `
            -Why ("Cada byte de DRAM estatica sai da heap que o TLS usa. " +
                  "Passou do teto: ou reduza um vetor estatico, ou rode o ensaio " +
                  "em hardware e, se o blkMin continuar folgado, suba o teto com " +
                  "'rbesp check --atualizar-baseline'.")
    } else {
        Add-GateRow -Board $board -Ok $true -WarnOnly `
            -Name 'DRAM estatica' -Detail "$usedDram B usados (sem baseline gravado)"
    }
    Add-GateRow -Board $board -Ok ($dramTotal - $usedDram -gt 60000) `
        -Name 'DRAM livre no link' `
        -Detail "$($dramTotal - $usedDram) B para heap" `
        -Why "Abaixo de ~60 KB livres no link nao sobra bloco contiguo para o record TLS."

    # --- 2. Tamanho contra o slot ---------------------------------------
    # O slot sai da tabela de particoes construida, nao de uma constante: no dia
    # em que a tabela mudar, um numero fixo aqui aprovaria um binario que nao
    # cabe -- e a falha so apareceria na placa, no meio da gravacao.
    if (Test-Path -LiteralPath $bin) {
        $binLen = (Get-Item -LiteralPath $bin).Length
        $slot = Get-SmallestAppPartition -BuildDir (Split-Path -Parent $bin)
        if ($slot -eq 0) {
            Add-GateRow -Board $board -Ok $false -Name 'Binario contra o slot OTA' `
                -Detail 'nao consegui ler a tabela de particoes construida' `
                -Why "Sem o tamanho real do slot nao da para afirmar que a imagem cabe."
        }
        else {
            $pct = [math]::Round(100.0 * $binLen / $slot, 1)
            Add-GateRow -Board $board -Ok ($binLen -lt ($slot * 0.92)) `
                -Name 'Binario contra o slot OTA' `
                -Detail "$binLen B de $slot B ($pct%)" `
                -Why "Acima de 92% do slot nao sobra espaco para a proxima versao crescer."
        }
    }

    # --- 3. Chaves de sdkconfig que ja quebraram o OTA -------------------
    $cfg = Get-SdkconfigMap -Path $cfgPath
    $inLen = if ($cfg.ContainsKey('CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN')) { [int] $cfg['CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN'] } else { 0 }
    Add-GateRow -Board $board -Ok ($inLen -eq 16384) `
        -Name 'MBEDTLS_SSL_IN_CONTENT_LEN' -Detail "$inLen" `
        -Why "O GitHub manda records de 16 KB. Com 4096 o handshake cai em -0x7200 (INVALID_RECORD)."

    $dyn = $cfg.ContainsKey('CONFIG_MBEDTLS_DYNAMIC_BUFFER')
    Add-GateRow -Board $board -Ok (-not $dyn) `
        -Name 'MBEDTLS_DYNAMIC_BUFFER desligado' -Detail $(if ($dyn) { 'ligado' } else { 'desligado' }) `
        -Why ("Ligado, o mbedtls devolve o bloco de 16 KB entre records e o reconquista " +
              "na leitura seguinte: uma aposta por record num download de 1,4 MB. " +
              "Medido na placa: blkMin caiu de 32768 para 20480 com ele ligado.")

    $keep = $cfg.ContainsKey('CONFIG_MBEDTLS_SSL_KEEP_PEER_CERTIFICATE')
    Add-GateRow -Board $board -Ok (-not $keep) `
        -Name 'KEEP_PEER_CERTIFICATE desligado' -Detail $(if ($keep) { 'ligado' } else { 'desligado' }) `
        -Why "Ninguem le o certificado do par depois do handshake e guarda-lo custa ~4 KB de heap."

    $lfn = $cfg.ContainsKey('CONFIG_FATFS_LFN_HEAP')
    Add-GateRow -Board $board -Ok $lfn `
        -Name 'FATFS com nome longo' -Detail $(if ($lfn) { 'LFN_HEAP' } else { 'ausente' }) `
        -Why "Sem LFN o FAT 8.3 recusa 'settings.json.tmp' e toda gravacao no cartao falha."

    $rollback = $cfg.ContainsKey('CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE')
    Add-GateRow -Board $board -Ok $rollback `
        -Name 'Rollback do bootloader' -Detail $(if ($rollback) { 'ligado' } else { 'desligado' }) `
        -Why "Sem rollback, uma imagem que nao bota deixa a placa so recuperavel por USB."

    # --- 3b. Chave de sdkconfig que o kconfig ignorou --------------------
    # Symbol errado nao quebra o build: vira um warning no meio de 1500
    # linhas e a opcao simplesmente nao vale. Foi o caso de
    # CONFIG_LV_USE_BTNMATRIX (nome do LVGL 8) sobrevivendo no LVGL 9.
    $defaultsPath = Join-Path $ProjectRoot 'firmware\ribanense-esp\sdkconfig.defaults'
    $mortasLigadas = @()   # pediu =y/valor e nao pegou: a opcao nao vale
    $mortasDesligadas = @() # pediu =n de algo que nem existe: so sujeira
    if ((Test-Path -LiteralPath $defaultsPath) -and $cfg.Count -gt 0) {
        $geradoRaw = Get-Content -LiteralPath $cfgPath -Raw
        foreach ($line in (Get-Content -LiteralPath $defaultsPath)) {
            if ($line -notmatch '^(CONFIG_[A-Z0-9_]+)=(.*)$') { continue }
            $chave = $Matches[1]
            $valor = $Matches[2].Trim()
            if ($valor -eq 'n') {
                if (-not ($geradoRaw -match [regex]::Escape("# $chave is not set")) -and
                    -not $cfg.ContainsKey($chave)) {
                    $mortasDesligadas += $chave
                }
            } elseif (-not $cfg.ContainsKey($chave)) {
                $mortasLigadas += $chave
            }
        }
    }
    Add-GateRow -Board $board -Ok ($mortasLigadas.Count -eq 0) `
        -Name 'Nenhuma opcao pedida foi ignorada' `
        -Detail $(if ($mortasLigadas.Count) { $mortasLigadas -join ', ' } else { "$($cfg.Count) chaves aplicadas" }) `
        -Why ("Estas chaves nao existem no kconfig desta versao do IDF/LVGL. " +
              "O build so emite um warning no meio de 1500 linhas e segue: " +
              "a opcao que voce pediu simplesmente nao esta valendo. " +
              "Foi o caso de CONFIG_LV_USE_BTNMATRIX (nome do LVGL 8) no LVGL 9.")
    if ($mortasDesligadas.Count) {
        Add-GateRow -Board $board -Ok $false -WarnOnly `
            -Name 'Chave =n de simbolo inexistente' -Detail ($mortasDesligadas -join ', ')
    }

    # --- 4. Pilha das tarefas que falam HTTPS ----------------------------
    # Pilha de tarefa FreeRTOS sai da mesma heap do TLS. Uma pilha de 32 KB
    # ja consumiu sozinha o maior bloco livre e matou o download.
    $otaSrc = Join-Path $ProjectRoot 'firmware\ribanense-esp\components\ota\ota.c'
    if (Test-Path -LiteralPath $otaSrc) {
        $otaText = Get-Content -LiteralPath $otaSrc -Raw
        $otaStack = 0
        if ($otaText -match '#define\s+OTA_TASK_STACK\s+(\d+)') { $otaStack = [int] $Matches[1] }
        Add-GateRow -Board $board -Ok ($otaStack -gt 0 -and $otaStack -le 12288) `
            -Name 'Pilha da tarefa de OTA' -Detail "$otaStack B" `
            -Why ("A pilha sai da mesma heap que o TLS precisa contigua. " +
                  "Com 32768 o download morre em 'alloc(16749 bytes) failed'.")
    }

    $bigStacks = @()
    $fwRoot = Join-Path $ProjectRoot 'firmware'
    foreach ($f in (Get-ChildItem -LiteralPath $fwRoot -Recurse -Filter *.c -ErrorAction SilentlyContinue)) {
        foreach ($m in [regex]::Matches((Get-Content -LiteralPath $f.FullName -Raw), 'xTaskCreate\w*\s*\([^;]*?,\s*(\d{5,})\s*,')) {
            if ([int] $m.Groups[1].Value -gt 16384) {
                $bigStacks += "$($f.Name):$($m.Groups[1].Value)"
            }
        }
    }
    Add-GateRow -Board $board -Ok ($bigStacks.Count -eq 0) `
        -Name 'Nenhuma tarefa com pilha gigante' `
        -Detail $(if ($bigStacks.Count) { $bigStacks -join ', ' } else { 'ok' }) `
        -Why "Pilha acima de 16 KB compete com o record TLS pelo maior bloco livre."

    # --- 5. Coerencia de versao -----------------------------------------
    # O CMake le version.json sem criar dependencia de configure: ja aconteceu
    # de o binario sair com a versao anterior gravada no app_desc.
    $verJson = Join-Path $ProjectRoot 'firmware\ribanense-esp\version.json'
    if ((Test-Path -LiteralPath $verJson) -and (Test-Path -LiteralPath $bin)) {
        $declared = [string] ((Get-Content -LiteralPath $verJson -Raw | ConvertFrom-Json).version)
        $desc = $null
        try { $desc = Get-EspAppDesc -BinPath $bin } catch { }
        if ($desc) {
            Add-GateRow -Board $board -Ok ([string] $desc.Version -eq $declared) `
                -Name 'Versao dentro do binario' `
                -Detail "app_desc='$($desc.Version)' version.json='$declared'" `
                -Why ("O CMake nao reconfigurou depois do version.json. " +
                      "Rode 'rbesp clean espelho' e compile de novo.")
        }
    }

    Write-Host ""
    if ($UpdateBaseline) {
        $path = Get-GateBaselinePath -ProjectRoot $ProjectRoot
        $novo = [ordered]@{
            comentario  = 'Tetos de memoria do OS. Subir so depois de um ensaio de OTA verde em hardware.'
            usedDramMax = $usedDram
            blkMinMin   = if ($base -and $base.blkMinMin) { [int] $base.blkMinMin } else { 24000 }
            tlsRecord   = $script:TlsRecordBytes
            medidoEm    = (Get-Date -Format 'yyyy-MM-dd')
        }
        ($novo | ConvertTo-Json) | Set-Content -LiteralPath $path -Encoding UTF8
        Write-Host "Baseline atualizado: usedDramMax=$usedDram" -ForegroundColor Cyan
    }

    if ($board.Failed -gt 0) {
        throw "$($board.Failed) gate(s) reprovado(s). Esta versao nao deve ser publicada."
    }
    Write-Host "Todos os gates passaram." -ForegroundColor Green
    return
}

<#
  Ensaio em hardware: manda a placa baixar o binario publicado inteiro pelo
  GitHub, conferir o sha256 e descartar. Nao apaga o slot nem troca o boot,
  entao pode rodar quantas vezes quiser.

  O ensaio roda com o httpd no ar e o cartao montado, ou seja, com menos RAM
  livre que o pull real: ensaio verde e piso, nao palpite.
#>
function Invoke-OtaRehearsal {
    param(
        [Parameter(Mandatory)] [string] $Ip,
        [int] $MinBlock = 24000,
        [int] $TimeoutSec = 240
    )
    Write-Host "Ensaio de OTA em $Ip — baixa o binario publicado e descarta." -ForegroundColor Cyan

    $antes = $null
    try {
        $antes = (Invoke-WebRequest -Uri "http://$Ip/status" -UseBasicParsing -TimeoutSec 15).Content | ConvertFrom-Json
    } catch {
        throw "Placa nao respondeu em http://$Ip/status : $($_.Exception.Message)"
    }
    Write-Host "Placa: $($antes.product) $($antes.version)  blk em repouso=$($antes.blk)"

    try {
        $null = Invoke-WebRequest -Uri "http://$Ip/rehearse" -UseBasicParsing -TimeoutSec 15
    } catch {
        throw ("A placa nao tem o endpoint /rehearse (firmware anterior a 0.4.0): " +
               "$($_.Exception.Message)")
    }

    $fim = (Get-Date).AddSeconds($TimeoutSec)
    $st = $null
    while ((Get-Date) -lt $fim) {
        Start-Sleep -Seconds 3
        try {
            $st = (Invoke-WebRequest -Uri "http://$Ip/status" -UseBasicParsing -TimeoutSec 10).Content | ConvertFrom-Json
        } catch { continue }
        Write-Host ("  {0,-24} blk={1} blkMin={2}" -f $st.ota, $st.blk, $st.blkMin)
        if ([string] $st.ota -like 'ensaio ok*') { break }
        if ([string] $st.ota -like '*falh*' -or [string] $st.ota -like '*sha256*') {
            throw "Ensaio reprovado na placa: $($st.ota)"
        }
    }
    if ($null -eq $st -or [string] $st.ota -notlike 'ensaio ok*') {
        throw "Ensaio nao terminou em $TimeoutSec s."
    }

    $blkMin = [int] $st.blkMin
    $margem = $blkMin - $script:TlsRecordBytes
    Write-Host ""
    Write-Host "Menor bloco contiguo durante o download: $blkMin B" -ForegroundColor Cyan
    Write-Host "Record TLS do GitHub: $($script:TlsRecordBytes) B  ->  margem de $margem B"
    if ($blkMin -lt $MinBlock) {
        throw ("Margem insuficiente: blkMin=$blkMin, minimo exigido $MinBlock. " +
               "A proxima versao que crescer um pouco quebra o OTA. " +
               "Reduza memoria estatica antes de publicar.")
    }
    Write-Host "Ensaio aprovado." -ForegroundColor Green
    return
}
