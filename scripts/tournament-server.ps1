<#
.SYNOPSIS
Starts and manages the Blobby Volley tournament server in Docker.

.DESCRIPTION
Commands: start, stop, restart, logs, status, update, configure, set-token.
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

# The checks match what the server accepts.
function Test-Site([string]$Value) {
    return $Value -cmatch '^https://[A-Za-z0-9.-]+(:[0-9]+)?$'
}

# 16 to 512 printable characters without spaces.
function Test-Token([string]$Value) {
    return $Value -cmatch '^[!-~]{16,512}$'
}

# Returns the port without leading zeros, or an empty string.
function Get-NormalPort([string]$Value) {
    if ($Value -cmatch '^[0-9]{1,5}$' -and [int]$Value -ge 1 -and [int]$Value -le 65535) { return "$([int]$Value)" }
    return ''
}

# Reads the saved settings. A value that is not valid is left empty, so it is asked for again.
function Read-Config {
    $config = @{ Site = ''; Token = ''; Port = '' }
    if (Test-Path -LiteralPath $ConfigFile) {
        foreach ($line in [System.IO.File]::ReadAllLines($ConfigFile)) {
            if ($line -match '^SITE_URL=(.*)$') { $config.Site = $Matches[1] }
            elseif ($line -match '^SITE_TOKEN=(.*)$') { $config.Token = $Matches[1] }
            elseif ($line -match '^PORT=(.*)$') { $config.Port = $Matches[1] }
        }
    }
    if ($config.Site -and -not (Test-Site $config.Site)) {
        Write-Host "The saved site address in $ConfigFile is not valid."
        $config.Site = ''
    }
    if ($config.Token -and -not (Test-Token $config.Token)) {
        Write-Host "The saved token in $ConfigFile is not valid."
        $config.Token = ''
    }
    if ($config.Port) {
        $port = Get-NormalPort $config.Port
        if (-not $port) { Write-Host "The saved port in $ConfigFile is not valid." }
        $config.Port = $port
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

# Asks for the site. Enter keeps the given value.
function Read-Site([string]$Default = '') {
    if (-not $Default) { $Default = $DefaultSite }
    while ($true) {
        $answer = Read-Host "Address of the tournament site [$Default]"
        if ([string]::IsNullOrWhiteSpace($answer)) { $answer = $Default }
        $answer = $answer.Trim().TrimEnd('/')
        if (Test-Site $answer) { return $answer }
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
        if (Test-Token $token) { return $token }
        Write-Host 'That is not a game server token. Copy the whole token from the admin page of the site.'
    }
}

# Asks for the port. Enter keeps the given value.
function Read-Port([string]$Default = '') {
    if (-not $Default) { $Default = "$DefaultPort" }
    while ($true) {
        $answer = Read-Host "UDP port for the players [$Default]"
        if ([string]::IsNullOrWhiteSpace($answer)) { $answer = $Default }
        $port = Get-NormalPort $answer.Trim()
        if ($port) { return $port }
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

$RetryHint = 'The old server container is removed. Fix the problem and run start again. Reports that wait stay in the data volume.'

# The optional hint is shown when Docker cannot start the server.
function New-ServerContainer($Config, [string]$Hint = '') {
    $envFile = Join-Path ([System.IO.Path]::GetTempPath()) ("blobby-server-" + [System.Guid]::NewGuid().ToString('N') + '.env')
    try {
        Write-PrivateFile $envFile "BLOBBY_TOURNAMENT=1`nBLOBBY_SITE_URL=$($Config.Site)`nBLOBBY_SITE_TOKEN=$($Config.Token)`nBLOBBY_PORT=$($Config.Port)`n"
        # The server listens on the port of the host, because the game moves to the port the server reports.
        & docker run -d --name $Container --restart unless-stopped -p "$($Config.Port):$($Config.Port)/udp" -v "${Volume}:/data" --env-file $envFile $Image | Out-Null
        $code = $LASTEXITCODE
    }
    finally {
        Remove-Item -LiteralPath $envFile -Force -ErrorAction SilentlyContinue
    }
    if ($code -ne 0) {
        if ($Hint) { Write-Host $Hint }
        Write-Fail 'Docker could not start the server. See the message above.'
    }
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

# Makes the container again with the saved settings, for example after they changed.
function Reset-ServerContainer($Config) {
    & docker image inspect $Image *> $null
    if ($LASTEXITCODE -ne 0) { Invoke-Pull }
    Remove-ServerContainer
    New-ServerContainer $Config $RetryHint
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
    Write-Host 'Usage: powershell -ExecutionPolicy Bypass -File tournament-server.ps1 <command>'
    Write-Host ''
    Write-Host '  start      Start the tournament server. Asks for the site and token the first time.'
    Write-Host '  stop       Stop the server. Reports that wait stay in the data volume.'
    Write-Host '  restart    Restart the server.'
    Write-Host '  logs       Follow the server log. Press Ctrl+C to stop following.'
    Write-Host '  status     Show whether the server runs and which site it uses.'
    Write-Host '  update     Download the newest server image and start the server on it.'
    Write-Host '  configure  Change the address of the site and the port.'
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
        # The old server keeps running if the download fails.
        Invoke-Pull
        Remove-ServerContainer
        New-ServerContainer $config $RetryHint
    }
    'configure' {
        Test-Docker
        $config = Read-Config
        $config.Site = Read-Site $config.Site
        $config.Port = Read-Port $config.Port
        if (-not $config.Token) { $config.Token = Read-Token }
        Save-Config $config
        Write-Host "Saved the settings in $ConfigFile"
        if ((Get-ContainerState) -eq 'running') { Reset-ServerContainer $config }
        else { Write-Host 'The next start uses them.' }
    }
    'set-token' {
        Test-Docker
        $config = Read-Config
        if (-not $config.Site) { $config.Site = Read-Site }
        if (-not $config.Port) { $config.Port = Read-Port }
        $config.Token = Read-Token
        Save-Config $config
        Write-Host "Saved the new token in $ConfigFile"
        Reset-ServerContainer $config
    }
    'help' { Show-Help }
    default {
        Write-Host "Unknown command: $Command"
        Show-Help
        exit 1
    }
}
