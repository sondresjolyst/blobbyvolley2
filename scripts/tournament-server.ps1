<#
.SYNOPSIS
Starts and manages the Blobby Volley tournament server in Docker.

.DESCRIPTION
Commands: start, stop, restart, logs, status, update, set-token.
The first start asks for the address of the tournament site, the game server
token from the admin page and the port. They are saved in a file that only
the current user can read. The token is passed to Docker in a temporary env
file, never on the command line.

.EXAMPLE
.\scripts\tournament-server.ps1 start
#>
[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '', Justification = 'An interactive script that talks to the user.')]
[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSUseShouldProcessForStateChangingFunctions', '', Justification = 'Internal helpers of the script.')]
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('start', 'stop', 'restart', 'logs', 'status', 'update', 'set-token', 'help')]
    [string]$Command = 'help'
)

Set-StrictMode -Version 2.0
# Not Stop, because Windows PowerShell 5.1 then fails on any stderr output
# of docker. Each docker call checks its exit code instead.
$ErrorActionPreference = 'Continue'

# The names match the command on the admin page of the site.
# The BLOBBY_SCRIPT_* variables exist only for testing the script.
# BLOBBY_SCRIPT_IMAGE can also pick another tag, such as sondresjo/blobby-server:dev.
$Container = if ($env:BLOBBY_SCRIPT_CONTAINER) { $env:BLOBBY_SCRIPT_CONTAINER } else { 'blobby-server' }
$Volume = if ($env:BLOBBY_SCRIPT_VOLUME) { $env:BLOBBY_SCRIPT_VOLUME } else { 'blobby-data' }
$Image = if ($env:BLOBBY_SCRIPT_IMAGE) { $env:BLOBBY_SCRIPT_IMAGE } else { 'sondresjo/blobby-server:latest' }
$DefaultSite = 'https://julalan.no'
$DefaultPort = 1234
$ConfigFile = if ($env:BLOBBY_SCRIPT_CONFIG) { $env:BLOBBY_SCRIPT_CONFIG } else { Join-Path $env:APPDATA 'blobby-server\tournament-server.conf' }

function Write-Fail([string]$Message) {
    Write-Host $Message -ForegroundColor Red
    exit 1
}

function Test-Docker {
    if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
        Write-Fail 'Docker is not installed. Install Docker Desktop from https://www.docker.com/products/docker-desktop and try again.'
    }
    & docker info *> $null
    if ($LASTEXITCODE -ne 0) {
        Write-Fail 'Docker is not running. Start Docker Desktop, wait until it says it is running, and try again.'
    }
}

# Gives only the current user access to a file, with no inherited rules.
function Set-OwnerOnly([string]$Path) {
    $user = [System.Security.Principal.WindowsIdentity]::GetCurrent().User
    $acl = New-Object System.Security.AccessControl.FileSecurity
    $acl.SetAccessRuleProtection($true, $false)
    $rule = New-Object System.Security.AccessControl.FileSystemAccessRule($user, 'FullControl', 'Allow')
    $acl.AddAccessRule($rule)
    # Set-Acl fails on a file that is already locked down, so use .NET.
    $item = Get-Item -LiteralPath $Path -ErrorAction Stop
    if ($item.PSObject.Methods['SetAccessControl']) { $item.SetAccessControl($acl) }
    else { [System.IO.FileSystemAclExtensions]::SetAccessControl($item, $acl) }
}

# Creates the file empty, locks it down, and only then writes the text.
function Write-PrivateFile([string]$Path, [string]$Text) {
    try {
        if (-not (Test-Path -LiteralPath $Path)) {
            New-Item -ItemType File -Path $Path -ErrorAction Stop | Out-Null
        }
        Set-OwnerOnly $Path
    }
    catch {
        Write-Fail "Could not protect $Path so only you can read it: $($_.Exception.Message)"
    }
    # Docker cannot read an env file that starts with a byte order mark.
    [System.IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding($false)))
}

function Read-Config {
    $config = @{ Site = ''; Token = ''; Port = '' }
    if (Test-Path -LiteralPath $ConfigFile) {
        foreach ($line in [System.IO.File]::ReadAllLines($ConfigFile)) {
            if ($line -match '^SITE_URL=(.*)$') { $config.Site = $Matches[1] }
            elseif ($line -match '^SITE_TOKEN=(.*)$') { $config.Token = $Matches[1] }
            elseif ($line -match '^PORT=(.*)$') { $config.Port = $Matches[1] }
        }
    }
    return $config
}

function Save-Config($Config) {
    $dir = Split-Path -Parent $ConfigFile
    if (-not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Path $dir -ErrorAction Stop | Out-Null
    }
    $text = "SITE_URL=$($Config.Site)`nSITE_TOKEN=$($Config.Token)`nPORT=$($Config.Port)`n"
    Write-PrivateFile $ConfigFile $text
}

function Read-Site {
    while ($true) {
        $answer = Read-Host "Address of the tournament site [$DefaultSite]"
        if ([string]::IsNullOrWhiteSpace($answer)) { $answer = $DefaultSite }
        $answer = $answer.Trim().TrimEnd('/')
        if ($answer -match '^https://[A-Za-z0-9.-]+(:[0-9]+)?$') { return $answer }
        Write-Host 'The address must start with https:// and hold only the host, for example https://julalan.no or https://dev.julalan.no.'
    }
}

function Read-Token {
    while ($true) {
        $secure = Read-Host 'Game server token from the admin page (hidden)' -AsSecureString
        $bstr = [System.Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
        try {
            $token = [System.Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr)
        }
        finally {
            [System.Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)
        }
        $token = $token.Trim()
        if ($token -match '^[A-Za-z0-9_-]{43}$') { return $token }
        Write-Host 'That is not a game server token. Copy all 43 characters from the admin page of the site.'
    }
}

function Read-Port {
    while ($true) {
        $answer = Read-Host "UDP port for the players [$DefaultPort]"
        if ([string]::IsNullOrWhiteSpace($answer)) { return "$DefaultPort" }
        $answer = $answer.Trim()
        if ($answer -match '^[0-9]{1,5}$' -and [int]$answer -ge 1 -and [int]$answer -le 65535) { return $answer }
        Write-Host 'The port must be a number from 1 to 65535.'
    }
}

function Get-Config {
    $config = Read-Config
    $changed = $false
    if (-not $config.Site) { $config.Site = Read-Site; $changed = $true }
    if (-not $config.Token) { $config.Token = Read-Token; $changed = $true }
    if (-not $config.Port) { $config.Port = Read-Port; $changed = $true }
    if ($changed) {
        Save-Config $config
        Write-Host "Saved the settings in $ConfigFile"
    }
    return $config
}

# Returns 'running', 'stopped' or 'missing'.
function Get-ContainerState {
    $state = & docker container inspect --format '{{.State.Running}}' $Container 2> $null
    if ($LASTEXITCODE -ne 0) { return 'missing' }
    if ("$state".Trim() -eq 'true') { return 'running' }
    return 'stopped'
}

function Write-FirewallHint($Port) {
    Write-Host ''
    Write-Host "Players on other machines need inbound UDP on port $Port through the Windows firewall."
    Write-Host 'If it is not open yet, run this once in PowerShell as administrator:'
    Write-Host "  New-NetFirewallRule -DisplayName 'Blobby Volley server' -Direction Inbound -Protocol UDP -LocalPort $Port -Action Allow"
}

function New-ServerContainer($Config) {
    $envFile = Join-Path ([System.IO.Path]::GetTempPath()) ("blobby-server-" + [System.Guid]::NewGuid().ToString('N') + '.env')
    try {
        Write-PrivateFile $envFile "BLOBBY_TOURNAMENT=1`nBLOBBY_SITE_URL=$($Config.Site)`nBLOBBY_SITE_TOKEN=$($Config.Token)`n"
        & docker run -d --name $Container --restart unless-stopped -p "$($Config.Port):1234/udp" -v "${Volume}:/data" --env-file $envFile $Image | Out-Null
        $code = $LASTEXITCODE
    }
    finally {
        Remove-Item -LiteralPath $envFile -Force -ErrorAction SilentlyContinue
    }
    if ($code -ne 0) { Write-Fail 'Docker could not start the server. See the message above.' }
    Write-Host "The tournament server runs on UDP port $($Config.Port) and reports to $($Config.Site)."
    Write-FirewallHint $Config.Port
}

function Remove-ServerContainer {
    if ((Get-ContainerState) -ne 'missing') {
        & docker rm -f $Container | Out-Null
        if ($LASTEXITCODE -ne 0) { Write-Fail 'Docker could not remove the old server container.' }
    }
}

function Invoke-Pull {
    & docker pull $Image
    if ($LASTEXITCODE -ne 0) { Write-Fail 'Docker could not download the server image. See the message above and check the internet connection.' }
}

function Invoke-Start {
    Test-Docker
    $config = Get-Config
    switch (Get-ContainerState) {
        'running' { Write-Host 'The tournament server is already running.' }
        'stopped' {
            & docker start $Container | Out-Null
            if ($LASTEXITCODE -ne 0) { Write-Fail 'Docker could not start the server. See the message above.' }
            Write-Host 'Started the tournament server again.'
            Write-FirewallHint $config.Port
        }
        default {
            & docker image inspect $Image *> $null
            if ($LASTEXITCODE -ne 0) { Invoke-Pull }
            New-ServerContainer $config
        }
    }
}

function Show-Help {
    Write-Host 'Usage: .\scripts\tournament-server.ps1 <command>'
    Write-Host ''
    Write-Host '  start      Start the tournament server. Asks for the site and token the first time.'
    Write-Host '  stop       Stop the server. Reports that wait stay in the data volume.'
    Write-Host '  restart    Restart the server.'
    Write-Host '  logs       Follow the server log. Press Ctrl+C to stop following.'
    Write-Host '  status     Show whether the server runs and which site it uses.'
    Write-Host '  update     Download the newest server image and start the server on it.'
    Write-Host '  set-token  Replace the game server token and start the server with it.'
    Write-Host ''
    Write-Host "Settings are kept in $ConfigFile"
}

switch ($Command) {
    'start' { Invoke-Start }
    'stop' {
        Test-Docker
        if ((Get-ContainerState) -eq 'running') {
            & docker stop $Container | Out-Null
            Write-Host 'Stopped the tournament server.'
        }
        else { Write-Host 'The tournament server is not running.' }
    }
    'restart' {
        Test-Docker
        if ((Get-ContainerState) -eq 'missing') { Invoke-Start }
        else {
            & docker restart $Container | Out-Null
            if ($LASTEXITCODE -ne 0) { Write-Fail 'Docker could not restart the server.' }
            Write-Host 'Restarted the tournament server.'
        }
    }
    'logs' {
        Test-Docker
        if ((Get-ContainerState) -eq 'missing') { Write-Fail 'The tournament server has not been started yet.' }
        & docker logs -f --tail 200 $Container
    }
    'status' {
        $config = Read-Config
        if ($config.Site) {
            Write-Host "Site:  $($config.Site)"
            Write-Host "Port:  $($config.Port)"
            Write-Host "Token: $(if ($config.Token) { 'saved' } else { 'missing' })"
        }
        else { Write-Host 'No settings saved yet. Run start first.' }
        Test-Docker
        switch (Get-ContainerState) {
            'running' { Write-Host 'Server: running' }
            'stopped' { Write-Host 'Server: stopped' }
            default { Write-Host 'Server: not created' }
        }
    }
    'update' {
        Test-Docker
        $config = Get-Config
        Invoke-Pull
        Remove-ServerContainer
        New-ServerContainer $config
    }
    'set-token' {
        Test-Docker
        $config = Read-Config
        if (-not $config.Site) { $config.Site = Read-Site }
        if (-not $config.Port) { $config.Port = Read-Port }
        $config.Token = Read-Token
        Save-Config $config
        Write-Host "Saved the new token in $ConfigFile"
        Remove-ServerContainer
        & docker image inspect $Image *> $null
        if ($LASTEXITCODE -ne 0) { Invoke-Pull }
        New-ServerContainer $config
    }
    default { Show-Help }
}
