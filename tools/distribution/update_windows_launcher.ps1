param([string]$PackagePath = $PSScriptRoot)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

function Update-WindowsLauncher([string]$Path, [string]$Payload) {
    if (Get-Process -Name X4 -ErrorAction SilentlyContinue) {
        throw 'Close X4 on Windows before applying this update.'
    }
    $root = (Resolve-Path -LiteralPath $Path).Path
    $packageFile = Join-Path $root 'package.json'
    if (!(Test-Path -LiteralPath $packageFile) -or !(Test-Path -LiteralPath (Join-Path $root 'setup.ps1'))) {
        throw 'Copy ALL files from the update ZIP into your existing Windows package folder (beside 3-START-X4-TEST.cmd), then run UPDATE-WINDOWS.cmd there.'
    }
    $pkg = Get-Content -Raw -LiteralPath $packageFile | ConvertFrom-Json
    if ($pkg.platform -ne 'Windows11' -or $pkg.release -ne 5) {
        throw 'This update requires a Windows v5 / 5.0.x package.'
    }
    if ($pkg.PSObject.Properties['version'] -and $pkg.version -notin @('5.0.0','5.0.1','5.0.2')) {
        throw 'This update supports versions 5.0.0, 5.0.1 and 5.0.2 only. Do not downgrade a newer package.'
    }
    $setup = Get-Content -Raw -LiteralPath $Payload
    $errors = $null
    [System.Management.Automation.Language.Parser]::ParseInput($setup, [ref]$null, [ref]$errors) | Out-Null
    if ($errors.Count) {
        throw 'The replacement launcher is damaged. Extract the update ZIP again.'
    }
    $pkg | Add-Member -NotePropertyName version -NotePropertyValue '5.0.2' -Force
    $updates = [ordered]@{
        'setup.ps1' = $setup
        'VERSION.txt' = "5.0.2`n"
        'package.json' = ($pkg | ConvertTo-Json -Depth 20) + "`n"
    }
    $stateFile = Join-Path $root 'installation.json'
    if (Test-Path -LiteralPath $stateFile) {
        $state = Get-Content -Raw -LiteralPath $stateFile | ConvertFrom-Json
        $state | Add-Member -NotePropertyName version -NotePropertyValue '5.0.2' -Force
        $updates['installation.json'] = ($state | ConvertTo-Json -Depth 20) + "`n"
    }
    $checksumFile = Join-Path $root 'SHA256SUMS.txt'
    if (Test-Path -LiteralPath $checksumFile) {
        $updates['SHA256SUMS.txt'] = ''
    }
    $backup = Join-Path $root ('backups\launcher-before-5.0.2-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $backup -Force | Out-Null
    $existed = @{}
    foreach ($name in $updates.Keys) {
        $target = Join-Path $root $name
        $existed[$name] = Test-Path -LiteralPath $target
        if ($existed[$name]) {
            Copy-Item -LiteralPath $target -Destination (Join-Path $backup $name)
        }
    }
    $utf8 = New-Object Text.UTF8Encoding($false)
    $written = @()
    try {
        foreach ($name in $updates.Keys) {
            if ($name -eq 'SHA256SUMS.txt') {
                continue
            }
            $written += $name
            [IO.File]::WriteAllText((Join-Path $root $name), $updates[$name], $utf8)
        }
        if ($updates.Contains('SHA256SUMS.txt')) {
            # Update only entries changed here, retaining all other expected hashes.
            $lines = @(Get-Content -LiteralPath (Join-Path $backup 'SHA256SUMS.txt') | ForEach-Object {
                if ($_ -match '^([0-9a-fA-F]{64})  (.+)$' -and $updates.Contains($Matches[2])) {
                    $name = $Matches[2]
                    (Get-FileHash -LiteralPath (Join-Path $root $name) -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $name
                } else {
                    $_
                }
            })
            $written += 'SHA256SUMS.txt'
            [IO.File]::WriteAllLines($checksumFile, [string[]]$lines, $utf8)
        }
    } catch {
        foreach ($name in $written) {
            $target = Join-Path $root $name
            if ($existed[$name]) {
                Copy-Item -LiteralPath (Join-Path $backup $name) -Destination $target -Force
            }
            elseif (Test-Path -LiteralPath $target) {
                Remove-Item -LiteralPath $target
            }
        }
        throw
    }
    Write-Host 'Updated Windows launcher to 5.0.2 (OneDrive profile fix).'
    Write-Host ('Previous launcher and metadata backed up in: ' + $backup)
    Write-Host 'Your session key, host IP, installed mod and game profiles were not changed.'
    Write-Host 'The Linux host can keep its existing package and running server.'
    Write-Host 'Next: 3-START-X4-TEST.cmd. If installation had not completed, run 1-INSTALL.cmd first.'
}
try {
    Update-WindowsLauncher $PackagePath (Join-Path $PSScriptRoot 'setup-5.0.2.ps1')
    exit 0
} catch {
    Write-Host ('ERROR: ' + $_.Exception.Message) -ForegroundColor Red
    exit 1
}
