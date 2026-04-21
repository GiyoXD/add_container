# Vision Logistics - Fast Launch Script
$ErrorActionPreference = "SilentlyContinue"
$EnvReadyFile = "$PSScriptRoot\.env_ready"

# 1. Quick Check for Python
if (!(Get-Command python -ErrorAction SilentlyContinue)) {
    Write-Host "Error: Python is not installed." -ForegroundColor Red
    Pause; exit
}

# 2. Optimized Dependency Check
if (!(Test-Path $EnvReadyFile)) {
    Write-Host "Initial setup: Checking libraries..." -ForegroundColor Cyan
    # Check main imports in one go
    $CheckScript = "import google.generativeai, googleapiclient, google.oauth2; print('OK')"
    python -c "$CheckScript" 2>$null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Installing dependencies (first time only)..." -ForegroundColor Yellow
        python -m pip install google-generativeai google-api-python-client google-auth --quiet
    }
    New-Item -Path $EnvReadyFile -ItemType File > $null
}

# 3. Sync Data
Write-Host "Refreshing data..." -ForegroundColor Cyan
python "$PSScriptRoot\backend\sync_data.py"

# 4. Launch UI
Write-Host "Launching..." -ForegroundColor Green
python "$PSScriptRoot\backend\ui.py"
