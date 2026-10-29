param(
    [ValidateSet('Install','Start','Test','Diagnose','Remove','Version','CheckWorldProfile')][string]$Action = 'Install',
    [string]$GamePath = '',
    [string]$ServerIP = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
$PackageRoot = $PSScriptRoot
$Utf8 = New-Object System.Text.UTF8Encoding($false)
# Isolated test profile from package.json: 45841 (server, player 2) or 45839+K for
# player K, so several clients can share one test PC. Anything else is refused.
$ProfileId = '45841'
$packageFile = Join-Path $PackageRoot 'package.json'
if (Test-Path -LiteralPath $packageFile) {
    $packageProfile = (Get-Content -Raw -LiteralPath $packageFile | ConvertFrom-Json).PSObject.Properties['test_profile']
    if ($packageProfile) {
        $ProfileId = [string]$packageProfile.Value
    }
}
if ($ProfileId -notmatch '^4584[1-8]$') {
    throw 'Unsupported isolated test profile in package.json.'
}

function Get-PackageVersion {
    $path = Join-Path $PackageRoot 'VERSION.txt'
    if (Test-Path -LiteralPath $path) {
        return (Get-Content -Raw -LiteralPath $path).Trim()
    }
    return 'legacy (no VERSION.txt)'
}
function Read-State {
    $path = Join-Path $PackageRoot 'installation.json'
    if (!(Test-Path -LiteralPath $path)) {
        throw 'Run 01-INSTALL.cmd first.'
    }
    return Get-Content -Raw -LiteralPath $path | ConvertFrom-Json
}
function Assert-Closed {
    if (Get-Process -Name X4 -ErrorAction SilentlyContinue) {
        throw 'X4 is still running. Close it first.'
    }
}
function Get-SteamRoot {
    $candidates = @()
    try {
        $steamPath = (Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam' -ErrorAction Stop).SteamPath
        if ($steamPath) {
            $candidates += [string]$steamPath
        }
    } catch {}
    foreach ($folder in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
        if ($folder) {
            $candidates += [IO.Path]::Combine($folder, 'Steam')
        }
    }
    foreach ($steamPath in ($candidates | Select-Object -Unique)) {
        if ([IO.File]::Exists([IO.Path]::Combine($steamPath, 'steam.exe'))) {
            return $steamPath
        }
    }
    $steamPath = (Read-Host 'Steam was not found. Enter the folder containing steam.exe').Trim().Trim('"')
    if (!$steamPath -or ![IO.File]::Exists([IO.Path]::Combine($steamPath, 'steam.exe'))) {
        throw 'Steam was not found in that folder. Install Steam and sign in first.'
    }
    return $steamPath
}
function Find-Game([string]$SteamRoot) {
    $libraries = @($SteamRoot)
    $vdf = Join-Path $SteamRoot 'steamapps\libraryfolders.vdf'
    if (Test-Path -LiteralPath $vdf) {
        foreach ($regexMatch in [regex]::Matches((Get-Content -Raw -LiteralPath $vdf), '"path"\s+"([^"]+)"')) {
            $libraries += $regexMatch.Groups[1].Value.Replace('\\', '\')
        }
    }
    $found = @()
    foreach ($lib in ($libraries | Select-Object -Unique)) {
        # Join-Path can throw on an unplugged drive before Test-Path runs.
        if (![IO.Directory]::Exists($lib)) {
            Write-Host ('Skipping unavailable Steam library: ' + $lib)
            continue
        }
        $manifest = Join-Path $lib 'steamapps\appmanifest_392160.acf'
        if (!(Test-Path -LiteralPath $manifest)) {
            continue
        }
        $regexMatch = [regex]::Match((Get-Content -Raw -LiteralPath $manifest), '"installdir"\s+"([^"]+)"')
        if ($regexMatch.Success) {
            $candidate = Join-Path (Join-Path $lib 'steamapps\common') $regexMatch.Groups[1].Value
            if (Test-Path -LiteralPath (Join-Path $candidate 'X4.exe')) {
                $found += $candidate
            }
        }
    }
    $found = @($found | Select-Object -Unique)
    if ($found.Count -eq 1) {
        return $found[0]
    }
    if ($found.Count -gt 1) {
        $found | ForEach-Object {
            Write-Host $_
        }
    }
    return (Read-Host 'Enter the X4 game folder (Steam > Properties > Installed Files > Browse)').Trim('"')
}
function Assert-Game([string]$Path, [string]$Build) {
    if (!(Test-Path -LiteralPath (Join-Path $Path 'X4.exe'))) {
        throw 'X4.exe was not found in the selected folder.'
    }
    if (!(Test-Path -LiteralPath (Join-Path $Path 'lua51_64.dll'))) {
        throw 'lua51_64.dll is missing. This package requires the 64-bit Steam game.'
    }
    if ((Get-Content -Raw -LiteralPath (Join-Path $Path 'version.dat')).Trim() -ne '900') {
        throw 'This package requires X4 9.00.'
    }
    $steamApps = Split-Path -Parent (Split-Path -Parent $Path)
    $manifest = Get-Content -Raw -LiteralPath (Join-Path $steamApps 'appmanifest_392160.acf')
    $regexMatch = [regex]::Match($manifest, '"buildid"\s+"(\d+)"')
    if (!$regexMatch.Success -or $regexMatch.Groups[1].Value -ne $Build) {
        throw ('Steam build does not match this package. Expected: ' + $Build + '. Do not test with different builds.')
    }
}
function Assert-IPv4([string]$Address, [bool]$AllowLoopback = $false) {
    # Validate the dotted decimal text itself. .NET Framework IPAddress.Parse reads
    # leading-zero octets as octal, so 192.168.010.5 would silently become .8.5.
    $octet = '(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])'
    if ($Address -notmatch ('^' + $octet + '(\.' + $octet + '){3}$')) {
        throw 'Enter an IPv4 address such as 192.168.1.25 or the host ZeroTier IPv4, without leading zeros.'
    }
    $first = [int]$Address.Split('.')[0]
    if ($first -eq 0) {
        throw 'Enter the host LAN or ZeroTier IP, not localhost or 0.0.0.0.'
    }
    if ($first -ge 224) {
        throw 'Enter the host LAN or ZeroTier IP, not a broadcast, multicast or reserved address.'
    }
    if (!$AllowLoopback -and $first -eq 127) {
        throw 'Enter the host LAN or ZeroTier IP, not localhost.'
    }
    return $Address
}
function Get-DocumentsFolder {
    return [Environment]::GetFolderPath('MyDocuments')
}
function Read-Utf8Text([string]$Path) {
    # Windows PowerShell 5.1 Get-Content reads BOM-less UTF-8 as ANSI.
    return [IO.File]::ReadAllText($Path, $Utf8)
}
function Get-ConfigValue([xml]$Config, [string]$Key) {
    $node = $Config.DocumentElement.SelectSingleNode($Key)
    if ($null -eq $node) {
        return $null
    }
    return $node.InnerText
}
function Set-ConfigHost([string]$Text, [string]$Address) {
    # Internal guest packages carry the placeholder; the public launcher writes the
    # concrete host before calling Install. Both must name exactly one host entry.
    $entries = [regex]::Matches($Text, '(?m)^\s*host\s*=\s*"([^"]*)"\s*,?\s*$')
    if ($entries.Count -ne 1) {
        throw 'The package mod config.lua must contain exactly one host entry. Use the original package.'
    }
    $value = $entries[0].Groups[1]
    if ($value.Value -eq 'SET_HOST_IP_DURING_INSTALL') {
        return $Text.Substring(0, $value.Index) + $Address + $Text.Substring($value.Index + $value.Length)
    }
    if ($value.Value -ne $Address) {
        throw ('The package mod is configured for host ' + $value.Value + ', not ' + $Address + '. Use the original package.')
    }
    return $Text
}
function Test-SameVolume([string]$First, [string]$Second) {
    return [IO.Path]::GetPathRoot([IO.Path]::GetFullPath($First)) -eq [IO.Path]::GetPathRoot([IO.Path]::GetFullPath($Second))
}
# Separate steps so the temporary-fixture tests can inject failures.
function Copy-ModTree([string]$Source, [string]$Destination) {
    Copy-Item -LiteralPath $Source -Destination $Destination -Recurse
}
function Publish-StagedMod([string]$Staging, [string]$Target) {
    Move-Item -LiteralPath $Staging -Destination $Target
}
function Write-InstallationState([string]$Path, [string]$Text) {
    # A unique temporary name per attempt: never reuse or delete a file this
    # attempt did not create.
    $temp = $Path + '.' + [Guid]::NewGuid().ToString('N') + '.tmp'
    try {
        [IO.File]::WriteAllText($temp, $Text, $Utf8)
        Move-Item -LiteralPath $temp -Destination $Path -Force
    } finally {
        if (Test-Path -LiteralPath $temp) {
            Remove-Item -LiteralPath $temp -Force
        }
    }
}
function Initialize-ProfileMetadata {
    if ('X4Lan.ProfileMetadata' -as [type]) {
        return
    }
    # Open the item itself, without following a link or reading cloud file contents.
    # Windows PowerShell 5.1 does not expose every reparse tag through Get-Item.
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
namespace X4Lan {
    public static class ProfileMetadata {
        [StructLayout(LayoutKind.Sequential)]
        private struct AttributeTagInfo {
            public uint FileAttributes;
            public uint ReparseTag;
        }
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true, SetLastError = true)]
        private static extern SafeFileHandle CreateFileW(string path, uint access,
            uint share, IntPtr security, uint creation, uint flags, IntPtr template);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetFileInformationByHandleEx(SafeFileHandle handle,
            int infoClass, out AttributeTagInfo info, uint size);
        public static uint ReadTag(string path) {
            // OPEN_EXISTING, BACKUP_SEMANTICS | OPEN_REPARSE_POINT; share R/W/delete.
            using (SafeFileHandle handle = CreateFileW(path, 0, 7, IntPtr.Zero,
                    3, 0x02200000, IntPtr.Zero)) {
                if (handle.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
                AttributeTagInfo info;
                if (!GetFileInformationByHandleEx(handle, 9, out info, 8))
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                return info.ReparseTag;
            }
        }
        public static bool IsCloudTag(uint tag) {
            // IO_REPARSE_TAG_CLOUD and CLOUD_1..F, plus the legacy ONEDRIVE tag.
            // Do not accept symbolic links, junctions or unknown reparse providers.
            return (tag & 0xFFFF0FFFu) == 0x9000001Au || tag == 0x80000021u;
        }
    }
}
'@
}
function Get-ProfileReparseTag([string]$Path) {
    Initialize-ProfileMetadata
    return [X4Lan.ProfileMetadata]::ReadTag($Path)
}
function Assert-SafeProfileItem([string]$Path) {
    if (!(Test-Path -LiteralPath $Path)) {
        return
    }
    $item = Get-Item -LiteralPath $Path -Force
    if (!($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        return
    }
    try {
        $tag = Get-ProfileReparseTag $item.FullName
    } catch {
        throw ('Cannot verify the test profile item: ' + $Path + '. ' + $_.Exception.Message)
    }
    Initialize-ProfileMetadata
    if ([X4Lan.ProfileMetadata]::IsCloudTag($tag)) {
        return
    }
    throw ('Refusing to modify a linked or unsupported test profile item (tag 0x{0:X8}): {1}' -f $tag, $Path)
}
function Assert-EmptyTestProfile([string]$Profile) {
    if ((Split-Path -Leaf $Profile) -ne $ProfileId) {
        throw 'Unexpected test profile path.'
    }
    Assert-SafeProfileItem $Profile
    $saves = Join-Path $Profile 'save'
    if (Test-Path -LiteralPath $saves) {
        if (Get-ChildItem -LiteralPath $saves -Filter '*.xml*' -File) {
            throw 'The test profile contains saves. Back them up and use an empty test profile. Nothing will be deleted.'
        }
    }
}
function Assert-WorldProfile {
    Assert-Closed
    $state = Read-State
    if ((Split-Path -Leaf $state.profile) -ne $ProfileId) {
        throw 'Unexpected test profile path.'
    }
    foreach ($path in @($state.profile, (Join-Path $state.profile 'config.xml'), (Join-Path $state.profile 'save'))) {
        Assert-SafeProfileItem $path
    }
    $save = Join-Path $state.profile 'save'
    if (Test-Path -LiteralPath $save) {
        foreach ($item in Get-ChildItem -LiteralPath $save -Force) {
            Assert-SafeProfileItem $item.FullName
        }
    }
}
function Install-Package {
    Assert-Closed
    $pkg = Get-Content -Raw -LiteralPath (Join-Path $PackageRoot 'package.json') | ConvertFrom-Json
    $steam = Get-SteamRoot
    $game = $GamePath
    if (!$game) {
        $game = Find-Game $steam
    }
    $game = $game.Trim().Trim('"')
    if ([IO.Path]::GetFileName($game) -ieq 'X4.exe') {
        $game = [IO.Path]::GetDirectoryName($game)
    }
    if (![IO.Directory]::Exists($game)) {
        throw ('X4 installation folder is unavailable: ' + $game)
    }
    $game = (Resolve-Path -LiteralPath $game).Path
    Assert-Game $game $pkg.steam_build
    $address = $ServerIP
    if (!$address) {
        $address = Read-Host 'Host IP (LAN or ZeroTier; shown in the server window)'
    }
    $localHost = $pkg.PSObject.Properties['channel'] -and $pkg.channel -in @('public', 'internal') -and $pkg.slot -eq 'p1'
    $address = Assert-IPv4 $address.Trim() ([bool]$localHost)
    $documents = Get-DocumentsFolder
    if (!$documents) {
        throw 'The Windows Documents folder could not be located.'
    }
    $profile = Join-Path $documents ('Egosoft\X4\' + $ProfileId)
    Assert-EmptyTestProfile $profile
    $target = Join-Path $game 'extensions\x4_lan_lab'
    if (Test-Path -LiteralPath $target) {
        throw ('The mod folder already exists. Remove it with the original package first: ' + $target)
    }
    $configPath = Join-Path $profile 'config.xml'
    $configExisted = Test-Path -LiteralPath $configPath
    if ($configExisted) {
        Assert-SafeProfileItem $configPath
        [xml]$config = Read-Utf8Text $configPath
    } else {
        [xml]$config = '<root />'
    }
    if ($config.DocumentElement.Name -ne 'root') {
        throw 'Unknown config.xml format.'
    }
    # Validate the mod configuration before anything is written.
    $lua = Set-ConfigHost (Read-Utf8Text (Join-Path $PackageRoot 'extension\x4_lan_lab\ui\config.lua')) $address
    $backup = Join-Path $PackageRoot ('backups\' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
    New-Item -ItemType Directory -Force -Path $backup | Out-Null
    $backupConfig = Join-Path $backup 'config.xml'
    if ($configExisted) {
        Copy-Item -LiteralPath $configPath -Destination $backupConfig
    }
    $statePath = Join-Path $PackageRoot 'installation.json'
    $previousState = $null
    if (Test-Path -LiteralPath $statePath) {
        $previousState = [IO.File]::ReadAllBytes($statePath)
    }
    $extensions = Split-Path -Parent $target
    $extensionsCreated = !(Test-Path -LiteralPath $extensions)
    $profileCreated = !(Test-Path -LiteralPath $profile)
    $tempConfig = Join-Path $profile 'config.x4lan.tmp'
    $tempConfigExisted = Test-Path -LiteralPath $tempConfig
    # Stage on the game volume so publishing the mod is a single rename.
    $staging = Join-Path $extensions ('.x4lan-staging-' + [Guid]::NewGuid().ToString('N'))
    $configReplaced = $false
    $published = $false
    try {
        New-Item -ItemType Directory -Force -Path $extensions | Out-Null
        Copy-ModTree (Join-Path $PackageRoot 'extension\x4_lan_lab') $staging
        [IO.File]::WriteAllText((Join-Path $staging 'ui\config.lua'), $lua, $Utf8)
        [IO.File]::WriteAllText((Join-Path $staging '.x4lan-package-id'), $pkg.package_id, $Utf8)
        New-Item -ItemType Directory -Force -Path $profile | Out-Null
        $testSettings = @{autosave='false'; uisafemode='false'; fullscreen='false'; borderless='true'}
        foreach ($key in $testSettings.Keys) {
            $node = $config.DocumentElement.SelectSingleNode($key)
            if ($null -eq $node) {
                $node = $config.CreateElement($key)
                $config.DocumentElement.AppendChild($node) | Out-Null
            }
            $node.InnerText = $testSettings[$key]
        }
        Assert-SafeProfileItem $tempConfig
        $config.Save($tempConfig)
        Move-Item -LiteralPath $tempConfig -Destination $configPath -Force
        $configReplaced = $true
        Publish-StagedMod $staging $target
        $published = $true
        $state = @{ game=$game; profile=$profile; steam=(Join-Path $steam 'steam.exe'); server=$address; package_id=$pkg.package_id; version=(Get-PackageVersion) }
        Write-InstallationState $statePath ($state | ConvertTo-Json)
    } catch {
        $failure = $_
        # Undo only what this run created; restore the exact prior config and state.
        $problems = @()
        $steps = @(
            {
                if ($published -and (Test-Path -LiteralPath $target)) {
                        Remove-Item -LiteralPath $target -Recurse -Force
                    }
            },
            {
                if (Test-Path -LiteralPath $staging) {
                    Remove-Item -LiteralPath $staging -Recurse -Force
                }
            },
            {
                if ($extensionsCreated -and (Test-Path -LiteralPath $extensions) -and !(Get-ChildItem -LiteralPath $extensions -Force)) {
                    Remove-Item -LiteralPath $extensions -Force
                }
            },
            {
                if (!$tempConfigExisted -and (Test-Path -LiteralPath $tempConfig)) {
                    Remove-Item -LiteralPath $tempConfig -Force
                }
            },
            {
                if ($configReplaced) {
                    if ($configExisted) {
                        [IO.File]::Copy($backupConfig, $configPath, $true)
                    } else {
                        Remove-Item -LiteralPath $configPath -Force
                    }
                }
            },
            {
                if ($profileCreated -and (Test-Path -LiteralPath $profile) -and !(Get-ChildItem -LiteralPath $profile -Force)) {
                    Remove-Item -LiteralPath $profile -Force
                }
            },
            {
                if ($null -ne $previousState) {
                    [IO.File]::WriteAllBytes($statePath, $previousState)
                } elseif (Test-Path -LiteralPath $statePath) {
                    Remove-Item -LiteralPath $statePath -Force
                }
            }
        )
        foreach ($step in $steps) {
            try {
                & $step
            } catch {
                $problems += $_.Exception.Message
            }
        }
        if ($problems.Count) {
            throw ($failure.Exception.Message + ' Rollback incomplete: ' + ($problems -join '; ') + ' Backup: ' + $backup)
        }
        throw $failure
    }
    Write-Host 'Installed. Existing player profiles were not modified.'
    Write-Host ('Test profile: ' + $profile)
    Write-Host 'Installation complete. Follow the role-specific next step shown by the launcher.'
}
function Start-TestGame {
    Assert-Closed
    $state = Read-State
    $pkg = Get-Content -Raw -LiteralPath (Join-Path $PackageRoot 'package.json') | ConvertFrom-Json
    Assert-Game $state.game $pkg.steam_build
    Assert-EmptyTestProfile $state.profile
    $configPath = Join-Path $state.profile 'config.xml'
    Assert-SafeProfileItem $configPath
    [xml]$config = Read-Utf8Text $configPath
    if ($config.DocumentElement.Name -ne 'root') {
        throw 'Unknown config.xml format.'
    }
    if ((Get-ConfigValue $config 'autosave') -ne 'false' -or (Get-ConfigValue $config 'uisafemode') -ne 'false') {
        throw 'The test profile must keep autosave=false and uisafemode=false. See INSTALL.html.'
    }
    Write-Host 'Using your test profile display, graphics and control settings.'
    $log = Join-Path $state.profile 'x4lan-debug.txt'
    if (Test-Path -LiteralPath $log) {
        $logs = Join-Path $PackageRoot 'logs'
        New-Item -ItemType Directory -Force -Path $logs | Out-Null
        Copy-Item -LiteralPath $log -Destination (Join-Path $logs ('x4-previous-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.txt'))
    }
    Start-Process -FilePath $state.steam -ArgumentList @('-applaunch','392160','-personalfolderid',$ProfileId,'-module','custom_creative','-skipintro','-debug','scripts','-logfile','x4lan-debug.txt','-nocputhrottle','-noworkshopsync')
    Write-Host 'Steam is starting a new creative sandbox. Do not load an existing game.'
}
function Test-ConnectionToRelay {
    $state = Read-State
    $cfg = Get-Content -Raw -LiteralPath (Join-Path $PackageRoot 'session.json') | ConvertFrom-Json
    $nonce = [Guid]::NewGuid().ToString('N')
    $udp = New-Object System.Net.Sockets.UdpClient
    $udp.Client.ReceiveTimeout = 2500
    try {
        $udp.Connect($state.server, [int]$cfg.port)
        $message = [Text.Encoding]::ASCII.GetBytes(('PING|1|' + $cfg.token + '|' + $nonce))
        $ok = $false
        for ($i=0; $i -lt 3; $i++) {
            $udp.Send($message,$message.Length) | Out-Null
            try {
                $sender = New-Object System.Net.IPEndPoint([Net.IPAddress]::Any,0)
                $reply = [Text.Encoding]::ASCII.GetString($udp.Receive([ref]$sender))
                if ($reply -eq ('PONG|1|' + $nonce + '|' + $cfg.build + '|' + $cfg.bubble)) {
                    $ok = $true
                    break
                }
            } catch [System.Net.Sockets.SocketException] { }
        }
        if (!$ok) {
            throw 'No matching server reply. Check the server window, host LAN/ZeroTier IP, matching package pair and Linux firewall. See INSTALL.html.'
        }
        Write-Host 'CONNECTION OK. The correct server and matching package replied. This check does not occupy a player slot.'
    } finally {
        $udp.Dispose()
    }
}
function Write-Diagnose {
    $state = Read-State
    $logs = Join-Path $PackageRoot 'logs'
    New-Item -ItemType Directory -Force -Path $logs | Out-Null
    $source = Join-Path $state.profile 'x4lan-debug.txt'
    if (Test-Path -LiteralPath $source) {
        Copy-Item -LiteralPath $source -Destination (Join-Path $logs 'x4-debug.txt') -Force
        $lines = @(Get-Content -LiteralPath $source | Where-Object {
            $_ -match '\[X4LAN\]'
        })
        [IO.File]::WriteAllLines((Join-Path $logs 'x4-mod-diagnostics.txt'), [string[]]$lines, $Utf8)
        $lines | Select-Object -Last 12 | ForEach-Object {
            Write-Host $_
        }
    } else {
        Write-Host 'No game log found yet.'
    }
    Write-Host ('Diagnostics folder: ' + $logs)
}
function Get-RemovedModArchive([string]$Game) {
    # A directory move is a rename and cannot cross volumes. Archive beside the
    # package when possible, otherwise in the game folder outside extensions\.
    $name = 'removed-mod-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff')
    $packageArchive = Join-Path $PackageRoot ('backups\' + $name)
    if (Test-SameVolume $Game $packageArchive) {
        return $packageArchive
    }
    return Join-Path $Game ('x4lan-backups\' + $name)
}
function Remove-Package {
    Assert-Closed
    $state = Read-State
    $target = Join-Path $state.game 'extensions\x4_lan_lab'
    $marker = Join-Path $target '.x4lan-package-id'
    if (!(Test-Path -LiteralPath $marker) -or (Get-Content -Raw -LiteralPath $marker) -ne $state.package_id) {
        throw 'The installed mod cannot be matched to this package. No files were removed.'
    }
    $destination = Get-RemovedModArchive $state.game
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
    Move-Item -LiteralPath $target -Destination $destination
    Write-Host 'Test mod removed from X4. The test profile was retained; no saves were deleted.'
    Write-Host ('Removed mod archived in: ' + $destination)
}
try {
    Write-Host ('X4 LAN Lab ' + (Get-PackageVersion))
    switch ($Action) {
        'Version' { }
        'CheckWorldProfile' {
            Assert-WorldProfile
        }
        'Install' {
            Install-Package
        }
        'Start' {
            Start-TestGame
        }
        'Test' {
            Test-ConnectionToRelay
        }
        'Diagnose' {
            Write-Diagnose
        }
        'Remove' {
            Remove-Package
        }
    }
    exit 0
} catch {
    Write-Host ('ERROR: ' + $_.Exception.Message) -ForegroundColor Red
    exit 1
}
