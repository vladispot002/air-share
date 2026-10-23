# PowerShell script for building and packaging LAN-Drop on Windows 10/11 x64
[CmdletBinding()]
param(
    [string]$BuildType = "Release",
    [string]$QtDir = $env:QT_DIR,
    [switch]$RunTests = $true
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

$TestingFlag = if ($RunTests) { "ON" } else { "OFF" }
$CmakeArgs = @(
    "-B", $BuildDir,
    "-S", $RootDir,
    "-DCMAKE_BUILD_TYPE=$BuildType",
    "-DBUILD_TESTING=$TestingFlag"
)

$VcpkgRoot = $env:VCPKG_INSTALLATION_ROOT
if (-not $VcpkgRoot -and (Test-Path "C:\vcpkg")) {
    $VcpkgRoot = "C:\vcpkg"
}
if ($VcpkgRoot -and (Test-Path "$VcpkgRoot\scripts\buildsystems\vcpkg.cmake")) {
    Write-Host "    Using vcpkg toolchain from $VcpkgRoot"
    $CmakeArgs += "-DCMAKE_TOOLCHAIN_FILE=$VcpkgRoot\scripts\buildsystems\vcpkg.cmake"
    $CmakeArgs += "-DVCPKG_TARGET_TRIPLET=x64-windows"
}

cmake @CmakeArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed." }

Write-Host "==> [2/4] Building targets..." -ForegroundColor Yellow
cmake --build $BuildDir --config $BuildType --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed." }

if ($RunTests) {
    Write-Host "==> Running automated test suite before packaging..." -ForegroundColor Yellow
    $env:QT_QPA_PLATFORM = "offscreen"
    ctest --test-dir $BuildDir -C $BuildType --output-on-failure --verbose
    if ($LASTEXITCODE -ne 0) { throw "Tests failed. Packaging aborted." }
}

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
if (-not $WinDeployQt -and $QtDir -and (Test-Path (Join-Path $QtDir "bin\windeployqt.exe"))) {
    $WinDeployQt = (Join-Path $QtDir "bin\windeployqt.exe")
}
if (-not $WinDeployQt -and $env:QT_DIR -and (Test-Path (Join-Path $env:QT_DIR "bin\windeployqt.exe"))) {
    $WinDeployQt = (Join-Path $env:QT_DIR "bin\windeployqt.exe")
}

if ($WinDeployQt) {
    $WinDeployCmd = if ($WinDeployQt -is [System.Management.Automation.CommandInfo]) { $WinDeployQt.Source } else { $WinDeployQt }
    Write-Host "    Running windeployqt ($WinDeployCmd)..."
    & $WinDeployCmd --release --compiler-runtime --no-translations --dir $DeployDir (Join-Path $DeployDir "lan_drop.exe")
} else {
    throw "windeployqt not found. Make sure Qt bin directory is in PATH or QT_DIR is set."
}

# Copy OpenSSL runtime DLLs from vcpkg if present
if ($VcpkgRoot) {
    $VcpkgBin = Join-Path $VcpkgRoot "installed\x64-windows\bin"
    if (Test-Path $VcpkgBin) {
        Write-Host "    Copying OpenSSL DLLs from $VcpkgBin to deployment directory..."
        Get-ChildItem -Path $VcpkgBin -Filter "*.dll" | Copy-Item -Destination $DeployDir -Force
    }
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
