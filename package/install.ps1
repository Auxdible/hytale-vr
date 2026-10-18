# Hytale VR installer. Copies opengl32.dll and the HytaleVR folder next to
# HytaleClient.exe, or removes them with -Uninstall.
param(
    [switch]$Uninstall,
    [string]$ClientDir
)

$ErrorActionPreference = 'Stop'
$package = Split-Path -Parent $MyInvocation.MyCommand.Path
$files = Join-Path $package 'files'
$stateDir = Join-Path $env:LOCALAPPDATA 'HytaleVR'
$savedClient = Join-Path $stateDir 'client-path.txt'

function Say([string]$text, [string]$color = 'Gray') { Write-Host $text -ForegroundColor $color }

function Resolve-Client([string]$path) {
    if (-not $path) { return $null }
    $path = $path.Trim().Trim('"')
    if (Test-Path -LiteralPath $path -PathType Leaf) { $path = Split-Path -Parent $path }
    foreach ($suffix in @('', 'Client', 'game\latest\Client', 'install\release\package\game\latest\Client',
                          'Hytale\install\release\package\game\latest\Client')) {
        $candidate = if ($suffix) { Join-Path $path $suffix } else { $path }
        if (Test-Path -LiteralPath (Join-Path $candidate 'HytaleClient.exe') -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    return $null
}

function Find-Client {
    $running = Get-Process HytaleClient -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($running -and $running.Path) { return Resolve-Client $running.Path }
    if (Test-Path -LiteralPath $savedClient) {
        $saved = Resolve-Client (Get-Content -LiteralPath $savedClient -Raw)
        if ($saved) { return $saved }
    }
    return Resolve-Client (Join-Path $env:APPDATA 'Hytale')
}

function Pick-Client {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.OpenFileDialog
    $dialog.Title = 'Find HytaleClient.exe'
    $dialog.Filter = 'Hytale client|HytaleClient.exe'
    if ($dialog.ShowDialog() -eq 'OK') { return Resolve-Client $dialog.FileName }
    return $null
}

function Wait-GameClosed {
    while (Get-Process HytaleClient -ErrorAction SilentlyContinue) {
        Say 'Hytale is running. Close it, then press Enter.' Yellow
        [void](Read-Host)
    }
}

function Hash([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }

Say ''
Say 'Hytale VR' White
Say '---------'

$client = if ($ClientDir) { Resolve-Client $ClientDir } else { Find-Client }
if (-not $client) {
    Say "Couldn't find Hytale automatically. Pick HytaleClient.exe in the window that opens." Yellow
    $client = Pick-Client
}
if (-not $client) { throw 'No Hytale client folder was selected.' }
Say "Hytale client: $client"

$proxy = Join-Path $client 'opengl32.dll'
$dataDir = Join-Path $client 'HytaleVR'
$record = Join-Path $dataDir 'install.json'
$backup = Join-Path $dataDir 'backup\opengl32.dll'

Wait-GameClosed

if ($Uninstall) {
    if (-not (Test-Path -LiteralPath $record)) {
        Say 'Hytale VR is not installed in this folder.' Yellow
        exit 0
    }
    $info = Get-Content -LiteralPath $record -Raw | ConvertFrom-Json
    if ((Test-Path -LiteralPath $proxy) -and ((Hash $proxy) -eq $info.proxy_sha256)) {
        Remove-Item -LiteralPath $proxy -Force
    }
    if (Test-Path -LiteralPath $backup) {
        Move-Item -LiteralPath $backup -Destination $proxy -Force
        Say 'Put back the opengl32.dll that was there before Hytale VR.'
    }
    $pluginJar = Join-Path $env:APPDATA 'Hytale\UserData\Mods\HytaleVR.jar'
    if (Test-Path -LiteralPath $pluginJar) { Remove-Item -LiteralPath $pluginJar -Force }
    Remove-Item -LiteralPath $dataDir -Recurse -Force
    Say 'Hytale VR is uninstalled.' Green
    exit 0
}

if (-not (Test-Path -LiteralPath (Join-Path $files 'opengl32.dll'))) {
    throw "The package is incomplete: files\opengl32.dll is missing. Extract the whole zip first."
}

New-Item -ItemType Directory -Force -Path $dataDir | Out-Null

# Another mod's opengl32.dll (Aurora Shaders uses the same file name).
$ours = $false
if (Test-Path -LiteralPath $record) {
    $info = Get-Content -LiteralPath $record -Raw | ConvertFrom-Json
    $ours = (Test-Path -LiteralPath $proxy) -and ((Hash $proxy) -eq $info.proxy_sha256)
}
if ((Test-Path -LiteralPath $proxy) -and -not $ours) {
    Say ''
    Say 'Another mod already uses opengl32.dll in this folder (Aurora Shaders does).' Yellow
    Say 'Hytale VR and that mod cannot run at the same time yet.' Yellow
    $answer = Read-Host 'Set the other mod aside and install Hytale VR? Uninstalling puts it back. (y/n)'
    if ($answer -notmatch '^(y|yes)$') { Say 'Nothing was changed.'; exit 1 }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $backup) | Out-Null
    Move-Item -LiteralPath $proxy -Destination $backup -Force
}

Copy-Item -LiteralPath (Join-Path $files 'opengl32.dll') -Destination $proxy -Force
# The companion server plugin: hits where your hand swings, in singleplayer worlds.
$jar = Join-Path $files 'HytaleVR.jar'
if (Test-Path -LiteralPath $jar) {
    $mods = Join-Path $env:APPDATA 'Hytale\UserData\Mods'
    New-Item -ItemType Directory -Force -Path $mods | Out-Null
    Copy-Item -LiteralPath $jar -Destination (Join-Path $mods 'HytaleVR.jar') -Force
    Say "Copied the HytaleVR plugin to $mods. Turn it on in your world's mods list if Hytale asks."
}
Get-ChildItem -LiteralPath (Join-Path $files 'HytaleVR') -File | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $dataDir $_.Name) -Force
}
Get-ChildItem -LiteralPath $dataDir -File -Recurse | Unblock-File
Unblock-File -LiteralPath $proxy

$versionFile = Join-Path $package 'VERSION.txt'
$version = if (Test-Path -LiteralPath $versionFile) { (Get-Content -LiteralPath $versionFile -Raw).Trim() } else { 'unknown' }
@{
    version      = $version
    client       = $client
    proxy_sha256 = Hash $proxy
    installed_at = (Get-Date).ToString('s')
} | ConvertTo-Json | Set-Content -LiteralPath $record -Encoding UTF8

New-Item -ItemType Directory -Force -Path $stateDir | Out-Null
Set-Content -LiteralPath $savedClient -Value $client -Encoding UTF8

Say ''
Say 'Hytale VR is installed.' Green
Say 'Start SteamVR if you like (Hytale VR can also start it), then start Hytale.'
Say 'Press F6 in game for the VR menu, then choose Enter VR.'
Say 'If a Hytale update removes it, run Install.bat again.'
