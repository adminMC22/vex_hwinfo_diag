# upload_offsets.ps1
# ONE-CLICK offset updater
#
# 1. Fetches latest GWorld/FNamePool from bootmgfw/ValorantOffsets
# 2. Merges with your local offsets.json
# 3. Uploads to paste.c-net.org
# 4. Copies URL to clipboard
#
# Usage: .\upload_offsets.ps1
# After running, paste the URL into CurlSetup.hpp and recompile.

param(
    [string]$LocalJson = "VEX/offsets.json"
)

$ErrorActionPreference = "Stop"

Write-Host "=== VEX OFFSET UPDATER ===" -ForegroundColor Cyan

# Step 1: Fetch latest from bootmgfw GitHub
Write-Host "[*] Fetching latest offsets from GitHub..." -ForegroundColor Cyan
try {
    $gh = Invoke-RestMethod -Uri "https://raw.githubusercontent.com/bootmgfw/ValorantOffsets/main/Offsets/13.01.00.5090349.md" -TimeoutSec 15

    # Extract GWorld and FNamePool from markdown
    if ($gh -match 'GWorld:\s*(0x[0-9A-Fa-f]+)') { $gworld = $Matches[1] }
    if ($gh -match 'FNamePool:\s*(0x[0-9A-Fa-f]+)') { $fname = $Matches[1] }
    if ($gh -match 'FNameState:\s*(0x[0-9A-Fa-f]+)') { $fstate = $Matches[1] }

    Write-Host "  GWorld:    $gworld" -ForegroundColor Green
    Write-Host "  FNamePool: $fname" -ForegroundColor Green
    Write-Host "  FNameState: $fstate" -ForegroundColor Green
} catch {
    Write-Host "[!] GitHub fetch failed: $_" -ForegroundColor Yellow
    Write-Host "[!] Continuing with existing local values..." -ForegroundColor Yellow
}

# Step 2: Read local offsets.json
if (-not (Test-Path $LocalJson)) {
    Write-Host "[!] Local offsets.json not found at: $LocalJson" -ForegroundColor Red
    Write-Host "    Run this script from the vex_hwinfo/ directory"
    exit 1
}

$jsonContent = Get-Content $LocalJson -Raw | ConvertFrom-Json

# Step 3: Update core offsets from GitHub
if ($gworld) {
    # Map GitHub names to our JSON names
    $core = @{
        "fname_pool" = $fname
        "FNamePool"  = $fname
    }

    # Add GWorld if the key exists in our JSON
    if ($jsonContent.offsets.GWorld -ne $null) {
        $jsonContent.offsets.GWorld = $gworld
    }
    if ($jsonContent.offsets.fname_pool -ne $null) {
        $jsonContent.offsets.fname_pool = $fname
    }
    if ($jsonContent.offsets.FNamePool -ne $null) {
        $jsonContent.offsets.FNamePool = $fname
    }

    Write-Host "[+] Core offsets updated" -ForegroundColor Green
}

# Step 4: Save updated JSON
$jsonContent | ConvertTo-Json -Depth 10 | Set-Content $LocalJson -Encoding utf8
Write-Host "[*] Local offsets.json saved" -ForegroundColor Cyan

# Step 5: Upload to paste.c-net.org
Write-Host "[*] Uploading to paste.c-net.org..." -ForegroundColor Cyan
$raw = $jsonContent | ConvertTo-Json -Depth 10 -Compress

try {
    $resp = Invoke-WebRequest -Uri "https://paste.c-net.org/" `
        -Method POST `
        -Body $raw `
        -UseBasicParsing `
        -ContentType "application/octet-stream" `
        -TimeoutSec 30

    $url = $resp.Content.Trim()
    $url | Set-Clipboard

    Write-Host ""
    Write-Host "============================================" -ForegroundColor Cyan
    Write-Host "  DONE" -ForegroundColor Green
    Write-Host "============================================" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "URL: $url" -ForegroundColor Green
    Write-Host "(copied to clipboard)" -ForegroundColor Green
    Write-Host ""
    Write-Host "NEXT: Open VEX/include/utils/CurlSetup.hpp" -ForegroundColor Yellow
    Write-Host "      Replace OFFSET_URL_PRIMARY with this URL" -ForegroundColor Yellow
    Write-Host "      Then recompile (or just run setup.bat)" -ForegroundColor Yellow
}
catch {
    Write-Host "[!] Upload failed: $_" -ForegroundColor Red
    Write-Host "    Copy this JSON and paste manually at paste.c-net.org:" -ForegroundColor Yellow
    Write-Host $raw.Substring(0, [Math]::Min(200, $raw.Length)) + "..." -ForegroundColor Gray
}
