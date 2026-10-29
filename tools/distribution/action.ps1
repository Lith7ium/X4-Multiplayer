param([ValidateSet('PICKUP','TRADE')][string]$Verb)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
try {
    $cfg=Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'session.json') | ConvertFrom-Json
    $package=Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'package.json') | ConvertFrom-Json
    $state=Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'installation.json') | ConvertFrom-Json
    $pending=Join-Path $PSScriptRoot 'pending-action.json'
    if (Test-Path -LiteralPath $pending) {
        $action=Get-Content -Raw -LiteralPath $pending | ConvertFrom-Json
        if ($action.verb -ne $Verb) {
            throw 'Another action is pending. Retry that same command first.'
        }
    } else {
        $action = @{
            request = [Guid]::NewGuid().ToString('N')
            verb = $Verb
        }
        $action | ConvertTo-Json | Set-Content -LiteralPath $pending -Encoding ASCII
    }
    $request=$action.request
    $udp=New-Object System.Net.Sockets.UdpClient
    try {
        $udp.Connect($state.server,[int]$cfg.port)
        $udp.Client.ReceiveTimeout=800
        $packet=[Text.Encoding]::ASCII.GetBytes("ACTION|1|$($cfg.token)|$($package.slot)|$request|$Verb|energycells")
        $deadline=[DateTime]::UtcNow.AddSeconds(90)
        Write-Host 'Waiting for the host game to confirm. Keep both games running.'
        while ([DateTime]::UtcNow -lt $deadline) {
            [void]$udp.Send($packet,$packet.Length)
            $sender=New-Object Net.IPEndPoint([Net.IPAddress]::Any,0)
            try {
                $answer=[Text.Encoding]::ASCII.GetString($udp.Receive([ref]$sender))
            } catch [Net.Sockets.SocketException] {
                continue
            }
            $responseFields=$answer.Split('|')
            if ($responseFields.Length -ne 4 -or $responseFields[0] -ne 'RESULT' -or $responseFields[1] -ne '1' -or $responseFields[2] -ne $request) {
                continue
            }
            if ($responseFields[3] -eq 'PENDING') {
                Start-Sleep -Milliseconds 200
                continue
            }
            Remove-Item -LiteralPath $pending
            $messages = @{
                OK = 'Confirmed: cargo action completed in the host game.'
                REJECTED = 'Rejected: check distance, container, cargo capacity or dock.'
                BUSY = 'Station busy. Try again later.'
                CANCELLED = 'Connection interrupted. Check cargo; if uncertain, restart both test games.'
                NO_WORLD = 'The host game is not ready yet.'
                NO_PLAYER = 'Your game is not connected yet.'
                SESSION_FULL = 'Test limit reached. Restart both games and the server.'
            }
            Write-Host $messages[$responseFields[3]]
            if ($responseFields[3] -eq 'OK') {
                exit 0
            } else {
                exit 1
            }
        }
        Write-Host 'No final reply yet. Request saved. Retry the same command; its request ID will be reused.'
        exit 2
    } finally {
        $udp.Close()
    }
} catch {
    Write-Host ('ERROR: '+$_.Exception.Message)
    exit 1
}
