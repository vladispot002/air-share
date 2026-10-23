# PowerShell script for building and packaging LAN-Drop on Windows 10/11 x64
[CmdletBinding()]
param(
    [string]$BuildType = "Release",
    [string]$QtDir = $env:QT_DIR
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $ScriptDir
$BuildDir = Join-Path $RootDir "build-windows"
$DeployDir = Join-Path $RootDir "packaging\windows\deploy"
$DistDir = Join-Path $RootDir "dist\windows"

Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "  [LAN-Drop] Building and Packaging for Windows   " -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Cyan

# Check CMake
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Error "CMake not found in PATH. Please install CMake."
}

# 1. Configure and Build
Write-Host "==> [1/4] Configuring with CMake..." -ForegroundColor Yellow
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
cmake -B $BuildDir -S $RootDir -DCMAKE_BUILD_TYPE=$BuildType -DBUILD_TESTING=OFF
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed." }

Write-Host "==> [2/4] Building lan_drop executable..." -ForegroundColor Yellow
cmake --build $BuildDir --config $BuildType --target lan_drop --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed." }

$ExePath = Join-Path $BuildDir "$BuildType\lan_drop.exe"
if (-not (Test-Path $ExePath)) {
    $ExePath = Join-Path $BuildDir "lan_drop.exe"
}
if (-not (Test-Path $ExePath)) {
    throw "Executable lan_drop.exe not found at $ExePath"
}

# 2. Prepare Deployment Directory
Write-Host "==> [3/4] Preparing deployment directory with windeployqt..." -ForegroundColor Yellow
if (Test-Path $DeployDir) { Remove-Item -Recurse -Force $DeployDir }
New-Item -ItemType Directory -Force -Path $DeployDir | Out-Null

Copy-Item -Path $ExePath -Destination $DeployDir -Force

# Run windeployqt
$WinDeployQt = Get-Command windeployqt -ErrorAction SilentlyContinue
if ($WinDeployQt) {
    Write-Host "    Running windeployqt..."
    & $WinDeployQt.Source --release --compiler-runtime --no-translations --dir $DeployDir (Join-Path $DeployDir "lan_drop.exe")
} else {
    Write-Warning "windeployqt not found in PATH. Make sure Qt bin directory is in PATH."
}

# 3. Compile Inno Setup Script
Write-Host "==> [4/4] Compiling Windows Installer (Inno Setup)..." -ForegroundColor Yellow
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null

$Iscc = Get-Command iscc -ErrorAction SilentlyContinue
if (-not $Iscc) {
    $CommonPaths = @(
        "${env:ProgramFiles(x86)}\Inno Setup 6\iscc.exe",
        "${env:ProgramFiles}\Inno Setup 6\iscc.exe"
    )
    foreach ($p in $CommonPaths) {
        if (Test-Path $p) {
            $Iscc = $p
            break
        }
    }
}

if ($Iscc) {
    $IssPath = Join-Path $RootDir "packaging\windows\landrop.iss"
    Write-Host "    Running $Iscc on $IssPath..."
    & $Iscc $IssPath
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup compilation failed." }
    Write-Host "Installer created successfully in $DistDir" -ForegroundColor Green
} else {
    Write-Warning "Inno Setup Compiler (iscc.exe) not found. Setup script is ready at packaging\windows\landrop.iss"
    Write-Warning "Install Inno Setup 6 (winget install JRSoftware.InnoSetup or choco install innosetup) to compile."
}

Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "  Done!" -ForegroundColor Cyan
