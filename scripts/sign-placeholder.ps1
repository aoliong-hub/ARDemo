# Restore the PLACEHOLDER build-profile.json5 (no secrets, safe to commit).
# Run this BEFORE `git commit` so the real signing material never enters the repo.
#
# Usage:  powershell -ExecutionPolicy Bypass -File scripts\sign-placeholder.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$tpl = Join-Path $root 'build-profile.placeholder.json5'
$target = Join-Path $root 'build-profile.json5'
if (-not (Test-Path $tpl)) {
    Write-Error "build-profile.placeholder.json5 not found."
    exit 1
}
Copy-Item $tpl $target -Force
Write-Host "build-profile.json5 -> PLACEHOLDER (safe to commit)."
