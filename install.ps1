$ErrorActionPreference = "Stop"

$RepoUrl = "https://github.com/JustinOros/xteink-plugins.git"
$FallbackDir = Join-Path $env:USERPROFILE "xteink-plugins"

function Update-SessionPath {
    $machine = [Environment]::GetEnvironmentVariable("Path", "Machine")
    $user = [Environment]::GetEnvironmentVariable("Path", "User")
    $env:Path = "$machine;$user"
}

function Test-Command($name) {
    return [bool](Get-Command $name -ErrorAction SilentlyContinue)
}

function Install-Package($id, $label) {
    if (-not (Test-Command "winget")) {
        throw "winget was not found. Install $label manually, then run this script again."
    }
    Write-Host "Installing $label..." -ForegroundColor Cyan
    winget install --id $id -e --silent --accept-package-agreements --accept-source-agreements | Out-Host
    Update-SessionPath
}

function Invoke-WithRetry($label, [scriptblock]$action) {
    $maxAttempts = 3
    for ($attempt = 1; $attempt -le $maxAttempts; $attempt++) {
        $global:LASTEXITCODE = 0
        try {
            & $action
            if ($LASTEXITCODE -eq 0) { return }
            if ($LASTEXITCODE -eq 3) { throw "Stopped by you." }
        } catch {
            Write-Host $_.Exception.Message -ForegroundColor Red
            if ($_.Exception.Message -eq "Stopped by you.") { throw }
        }
        if ($attempt -lt $maxAttempts) {
            Write-Host "$label failed (attempt $attempt of $maxAttempts). Retrying in 10 seconds..." -ForegroundColor Yellow
            Start-Sleep -Seconds 10
        }
    }
    throw "$label failed after $maxAttempts attempts. Check your internet connection and run this script again."
}

function Test-PythonPath($path) {
    if (-not $path -or -not (Test-Path $path) -or ($path -match "WindowsApps")) { return $false }
    try {
        $version = [int](& $path -c "import sys; print(sys.version_info.major * 100 + sys.version_info.minor)" 2>$null | Out-String).Trim()
        return ($version -ge 310 -and $version -le 313)
    } catch {
        return $false
    }
}

function Get-PythonExe {
    foreach ($ver in @("313", "312", "311", "310")) {
        $candidates = @(
            (Join-Path $env:LOCALAPPDATA "Programs\Python\Python$ver\python.exe"),
            (Join-Path $env:ProgramFiles "Python$ver\python.exe")
        )
        foreach ($candidate in $candidates) {
            if (Test-PythonPath $candidate) { return $candidate }
        }
    }
    foreach ($ver in @("3.13", "3.12", "3.11", "3.10")) {
        try {
            $path = (& py "-$ver" -c "import sys; print(sys.executable)" 2>$null | Out-String).Trim()
            if (Test-PythonPath $path) { return $path }
        } catch {
        }
    }
    try {
        $path = (& python -c "import sys; print(sys.executable)" 2>$null | Out-String).Trim()
        if (Test-PythonPath $path) { return $path }
    } catch {
    }
    return $null
}

function Add-ToPath($dir) {
    if ($dir -and (Test-Path $dir) -and ($env:Path -notlike "*$dir*")) {
        $env:Path = "$dir;$env:Path"
    }
}

try {
    Write-Host ""
    Write-Host "XTEINK Plugins installer" -ForegroundColor Green
    Write-Host ""

    if (-not (Test-Command "git")) {
        Install-Package "Git.Git" "Git"
        Add-ToPath "$env:ProgramFiles\Git\cmd"
    }
    if (-not (Test-Command "git")) {
        throw "Git could not be installed. Install it from git-scm.com, then run this script again."
    }

    $python = Get-PythonExe
    if (-not $python) {
        Install-Package "Python.Python.3.12" "Python"
        $python = Get-PythonExe
    }
    if (-not $python) {
        throw "Python 3.12 could not be installed. Install Python 3.12 from python.org (tick 'Add python.exe to PATH'), then run this script again."
    }
    Write-Host "Using Python at $python" -ForegroundColor Cyan

    if (Test-Path (Join-Path $PSScriptRoot "install.py")) {
        $projectDir = $PSScriptRoot
    } elseif (Test-Path (Join-Path $FallbackDir "install.py")) {
        $projectDir = $FallbackDir
        Write-Host "Updating plugins..." -ForegroundColor Cyan
        git -C $projectDir pull | Out-Host
    } else {
        Write-Host "Downloading plugins..." -ForegroundColor Cyan
        git clone $RepoUrl $FallbackDir | Out-Host
        $projectDir = $FallbackDir
    }
    Set-Location $projectDir

    Write-Host "Installing required Python packages..." -ForegroundColor Cyan
    Invoke-WithRetry "Installing Python packages" {
        & $python -m pip install --upgrade pip | Out-Host
        & $python -m pip install "platformio==6.1.19" | Out-Host
        if (Test-Path "requirements.txt") {
            & $python -m pip install -r requirements.txt | Out-Host
        }
    }

    $scriptsDir = & $python -c "import sysconfig; print(sysconfig.get_path('scripts'))"
    $userScriptsDir = & $python -c "import sysconfig; print(sysconfig.get_path('scripts', 'nt_user'))"
    Add-ToPath $scriptsDir
    Add-ToPath $userScriptsDir
    Add-ToPath (Join-Path $env:USERPROFILE ".platformio\penv\Scripts")

    if (-not (Test-Command "pio")) {
        throw "PlatformIO was installed but could not be found. Close this window, open PowerShell again and run this script once more."
    }

    Write-Host ""
    Write-Host "Everything needed is installed." -ForegroundColor Green
    $answer = Read-Host "Default Yes to all prompts? [Y/n]"
    $installerArgs = @()
    if ($answer -eq "" -or $answer -match "^[Yy]") {
        $installerArgs += "--yes"
    }
    $installerArgs += $args

    Write-Host ""
    Write-Host "Connect your XTEINK reader with a USB cable and turn it on." -ForegroundColor Yellow
    Read-Host "Press Enter when it is connected"

    Invoke-WithRetry "The installer" {
        & $python install.py @installerArgs
    }

    Write-Host ""
    Write-Host "All done. On the reader, open Settings and choose Plugins at the bottom." -ForegroundColor Green
} catch {
    Write-Host ""
    Write-Host "Something went wrong:" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
}

Write-Host ""
Read-Host "Press Enter to close"
