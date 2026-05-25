# Restore the REAL signing config into build-profile.json5 for local command-line builds.
#
# build-profile.json5 is committed as a placeholder (no secrets). The real signing material
# lives in build-profile.local.json5 (gitignored). Run this BEFORE `hvigorw assembleHap`,
# then run sign-placeholder.ps1 BEFORE committing.
#
# Usage:  powershell -ExecutionPolicy Bypass -File scripts\sign-real.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$local = Join-Path $root 'build-profile.local.json5'
$target = Join-Path $root 'build-profile.json5'
if (-not (Test-Path $local)) {
    Write-Error "build-profile.local.json5 not found. Create it once from your DevEco auto-generated signing (copy your working build-profile.json5 to build-profile.local.json5). See README."
    exit 1
}
Copy-Item $local $target -Force
Write-Host "build-profile.json5 -> REAL signing (local build only). Run scripts\sign-placeholder.ps1 before committing."
