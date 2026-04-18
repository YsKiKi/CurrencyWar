# build_all.ps1 - Full background build script
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8
chcp 65001 | Out-Null
$ErrorActionPreference = "Continue"
$env:HTTP_PROXY = "http://127.0.0.1:7890"
$env:HTTPS_PROXY = "http://127.0.0.1:7890"

$root = "D:\0.PC\Desktop\CurrencyWar_Cpp"
$vcpkg = "$root\vcpkg\vcpkg.exe"
$logFile = "$root\build_log.txt"

function Log($msg) {
    $ts = Get-Date -Format "HH:mm:ss"
    $line = "[$ts] $msg"
    Write-Host $line
    Add-Content -Path $logFile -Value $line
}

Set-Content -Path $logFile -Value "=== CurrencyWar Build Log ===" -Force
Log "Starting build pipeline..."

# Wait for any existing vcpkg to finish
$lockFile = "$root\vcpkg\installed\vcpkg\vcpkg-running.lock"
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

Log "Step 3: Cleaning old build dir..."
if (Test-Path "$root\build") {
    Remove-Item -Recurse -Force "$root\build"
}

Log "Step 4: CMake configure..."
$paddleDir = "$root\paddle_inference_prod"
if (-not (Test-Path "$paddleDir\paddle\lib\paddle_inference.lib")) {
    $paddleDir = ""
    Log "WARNING: paddle_inference_prod not found, will use stub"
}
$cmakeArgs = @(
    "-B", "$root\build", "-S", "$root",
    "-DCMAKE_TOOLCHAIN_FILE=$root\vcpkg\scripts\buildsystems\vcpkg.cmake",
    "-G", "Visual Studio 17 2022", "-A", "x64",
    "-DVCPKG_MANIFEST_MODE=OFF",
    "-DCMAKE_PREFIX_PATH=$root\vcpkg\installed\x64-windows"
)
if ($paddleDir) {
    $cmakeArgs += "-DPADDLE_INFERENCE_DIR=$paddleDir"
}
& "C:\Program Files\CMake\bin\cmake.exe" @cmakeArgs 2>&1 | Tee-Object -Append -FilePath $logFile

if ($LASTEXITCODE -ne 0) {
    Log "ERROR: CMake configure failed with exit code $LASTEXITCODE"
    exit 1
}

Log "Step 5: Building (Release)..."
& "C:\Program Files\CMake\bin\cmake.exe" --build "$root\build" --config Release 2>&1 | Tee-Object -Append -FilePath $logFile

if ($LASTEXITCODE -ne 0) {
    Log "ERROR: Build failed with exit code $LASTEXITCODE"
    exit 1
}

Log "Build succeeded! Output: $root\build\Release\"
Log "=== BUILD COMPLETE ==="
