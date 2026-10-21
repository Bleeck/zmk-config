# Builds the firmware locally in Docker, the same way the GitHub workflow does.
# Output: firmware\<date_time>\*.uf2 in the repo root, a new folder per build; old builds are kept.
# -Clean deletes the cached west workspace and builds from scratch.
param([switch]$Clean)

$ErrorActionPreference = 'Stop'

# ZMK's build image for Zephyr 3.5 (ZMK v0.3), pinned by digest so it can't change under us.
$Image = 'zmkfirmware/zmk-build-arm:3.5-branch-20250930025731-3.5.0-0.16.3-f93290b3ab1a-18117245422@sha256:840526f03c19c286b614d5cb97b11242ae19e468fc2ef2f095f6cf0465fa1c5b'
$Volume = 'zmk-config-bleeckeys-workspace'

$Repo = Split-Path -Parent $PSScriptRoot
$Out = Join-Path $Repo 'firmware' (Get-Date -Format 'yyyy-MM-dd_HH-mm-ss')

function Test-Docker {
    docker info *> $null
    return $LASTEXITCODE -eq 0
}

if (-not (Test-Docker)) {
    $desktop = @(
        "$env:LOCALAPPDATA\Programs\DockerDesktop\Docker Desktop.exe",
        "$env:ProgramFiles\Docker\Docker\Docker Desktop.exe"
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $desktop) { throw 'Docker is not running and Docker Desktop was not found.' }

    Write-Host 'Starting Docker Desktop...'
    Start-Process $desktop
    $deadline = (Get-Date).AddMinutes(3)
    while (-not (Test-Docker)) {
        if ((Get-Date) -gt $deadline) { throw 'Docker Desktop did not start within 3 minutes.' }
        Start-Sleep -Seconds 3
    }
}

if ($Clean) {
    docker volume rm $Volume 2>$null | Out-Null
}

New-Item -ItemType Directory -Force $Out | Out-Null

docker run --rm `
    -v "${Repo}:/zmk-config:ro" `
    -v "${Volume}:/workspace" `
    -v "${Out}:/out" `
    $Image bash /zmk-config/local-build/build.sh

if ($LASTEXITCODE -ne 0) { throw "Build failed (exit code $LASTEXITCODE)." }

Write-Host "Firmware is in $Out"
explorer.exe $Out
