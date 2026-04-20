# build_all.ps1 - Full background build script
# Usage: .\build_all.ps1 [-Proxy "http://127.0.0.1:7890"]
param(
    [string]$Proxy = ""
)

[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8
chcp 65001 | Out-Null
$ErrorActionPreference = "Continue"

# Proxy: only set if user provides it
if ($Proxy) {
    $env:HTTP_PROXY = $Proxy
    $env:HTTPS_PROXY = $Proxy
}

# Auto-detect project root (directory where this script lives)
$root = Split-Path -Parent $MyInvocation.MyCommand.Definition
$vcpkg = "$root\vcpkg\vcpkg.exe"
$logFile = "$root\build_log.txt"

# Auto-detect cmake
$cmake = Get-Command cmake -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
if (-not $cmake) {
    # Fallback to common install paths
    $candidates = @(
        "C:\Program Files\CMake\bin\cmake.exe",
        "C:\Program Files (x86)\CMake\bin\cmake.exe"
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { $cmake = $c; break }
    }
    if (-not $cmake) {
        Write-Host "ERROR: cmake not found. Please install CMake and add it to PATH."
        exit 1
    }
}

function Log($msg) {
    $ts = Get-Date -Format "HH:mm:ss"
    $line = "[$ts] $msg"
    Write-Host $line
    Add-Content -Path $logFile -Value $line
}

Set-Content -Path $logFile -Value "=== CurrencyWar Build Log ===" -Force
Log "Starting build pipeline..."
Log "Project root: $root"
Log "CMake: $cmake"

# ── Step 0: Bootstrap vcpkg if needed ──
if (-not (Test-Path $vcpkg)) {
    Log "Step 0: Bootstrapping vcpkg..."
    $bootstrapScript = "$root\vcpkg\bootstrap-vcpkg.bat"
    if (-not (Test-Path $bootstrapScript)) {
        Log "ERROR: vcpkg submodule not found. Run: git submodule update --init"
        exit 1
    }
    & $bootstrapScript 2>&1 | Tee-Object -Append -FilePath $logFile
    if (-not (Test-Path $vcpkg)) {
        Log "ERROR: vcpkg bootstrap failed"
        exit 1
    }
    Log "vcpkg bootstrapped successfully."
} else {
    Log "Step 0: vcpkg already exists, skipping bootstrap."
}

# Wait for any existing vcpkg to finish
Log "Waiting for existing vcpkg processes to finish..."
while ($true) {
    $vcpkgProcs = Get-Process -Name vcpkg -ErrorAction SilentlyContinue
    if (-not $vcpkgProcs) { break }
    Write-Host "." -NoNewline
    [System.Threading.Thread]::Sleep(5000)
}
Log "No vcpkg processes running. Proceeding..."

Log "Step 1: Installing opencv4 + nlohmann-json..."
& $vcpkg install opencv4:x64-windows nlohmann-json:x64-windows --classic 2>&1 | Tee-Object -Append -FilePath $logFile
if ($LASTEXITCODE -ne 0) {
    Log "WARNING: opencv4/nlohmann-json install returned $LASTEXITCODE"
}

Log "Step 2: Installing qtbase..."
& $vcpkg install qtbase:x64-windows --classic 2>&1 | Tee-Object -Append -FilePath $logFile
if ($LASTEXITCODE -ne 0) {
    Log "ERROR: qtbase install failed with exit code $LASTEXITCODE"
    exit 1
}

# ── Step 2.5: Download Paddle Inference if not present ──
$paddleDir = "$root\paddle_inference_prod"
$paddleLib = "$paddleDir\paddle\lib\paddle_inference.lib"
if (-not (Test-Path $paddleLib)) {
    Log "Step 2.5: Downloading Paddle Inference C++ library..."
    $paddleUrl = "https://paddle-inference-lib.bj.bcebos.com/3.0.0/cxx_c/Windows/CPU/x86-64_avx-openblas/paddle_inference.zip"
    $paddleZip = "$root\paddle_inference.zip"
    try {
        Log "Downloading from $paddleUrl ..."
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -Uri $paddleUrl -OutFile $paddleZip -UseBasicParsing
        Log "Extracting Paddle Inference..."
        Expand-Archive -Path $paddleZip -DestinationPath "$root\_paddle_tmp" -Force
        # The zip usually contains a top-level folder; move its contents
        $extracted = Get-ChildItem "$root\_paddle_tmp" -Directory | Select-Object -First 1
        if ($extracted) {
            if (Test-Path $paddleDir) { Remove-Item -Recurse -Force $paddleDir }
            Move-Item -Path $extracted.FullName -Destination $paddleDir
        }
        Remove-Item -Recurse -Force "$root\_paddle_tmp" -ErrorAction SilentlyContinue
        Remove-Item -Force $paddleZip -ErrorAction SilentlyContinue
        Log "Paddle Inference downloaded and extracted to $paddleDir"
    } catch {
        Log "WARNING: Failed to download Paddle Inference: $_"
        Log "You can manually download from: https://www.paddlepaddle.org.cn/inference/master/guides/install/download_lib.html"
        Log "Extract to: $paddleDir"
    }
}

Log "Step 3: Cleaning old build dir..."
if (Test-Path "$root\build") {
    Remove-Item -Recurse -Force "$root\build"
}

Log "Step 4: CMake configure..."
$paddleCmakeArg = ""
if (Test-Path "$paddleDir\paddle\lib\paddle_inference.lib") {
    $paddleCmakeArg = "-DPADDLE_INFERENCE_DIR=$paddleDir"
    Log "Using Paddle Inference from: $paddleDir"
} else {
    Log "WARNING: paddle_inference_prod not found, will use stub"
}
$cmakeArgs = @(
    "-B", "$root\build", "-S", "$root",
    "-DCMAKE_TOOLCHAIN_FILE=$root\vcpkg\scripts\buildsystems\vcpkg.cmake",
    "-G", "Visual Studio 17 2022", "-A", "x64",
    "-DVCPKG_MANIFEST_MODE=OFF",
    "-DCMAKE_PREFIX_PATH=$root\vcpkg\installed\x64-windows"
)
if ($paddleCmakeArg) {
    $cmakeArgs += $paddleCmakeArg
}
& $cmake @cmakeArgs 2>&1 | Tee-Object -Append -FilePath $logFile

if ($LASTEXITCODE -ne 0) {
    Log "ERROR: CMake configure failed with exit code $LASTEXITCODE"
    exit 1
}

Log "Step 5: Building (Release)..."
& $cmake --build "$root\build" --config Release 2>&1 | Tee-Object -Append -FilePath $logFile

if ($LASTEXITCODE -ne 0) {
    Log "ERROR: Build failed with exit code $LASTEXITCODE"
    exit 1
}

Log "Build succeeded! Output: $root\build\Release\"
Log "=== BUILD COMPLETE ==="
