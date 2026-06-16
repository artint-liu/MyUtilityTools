# ============================================
#  eext-batch-led-place 打包脚本
#  编译 TypeScript 并打包为 .eext 扩展文件
# ============================================

$ErrorActionPreference = "Stop"
$ProjectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ProjectDir

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  eext-batch-led-place Build & Pack" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# 1. Check node/npm
Write-Host "[Step 1] Checking environment..." -ForegroundColor Yellow
if (-not (Get-Command npm -ErrorAction SilentlyContinue)) {
    Write-Host "[ERROR] npm not found, please install Node.js first" -ForegroundColor Red
    exit 1
}
Write-Host "  npm: $(npm --version)" -ForegroundColor Green
Write-Host ""

# 2. Install dependencies
if (-not (Test-Path "node_modules")) {
    Write-Host "[Step 2] Installing dependencies..." -ForegroundColor Yellow
    npm install
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ERROR] npm install failed" -ForegroundColor Red
        exit 1
    }
    Write-Host ""
} else {
    Write-Host "[Step 2] Dependencies already installed, skipping npm install" -ForegroundColor Green
    Write-Host ""
}

# 3. Compile TypeScript
Write-Host "[Step 3] Compiling TypeScript..." -ForegroundColor Yellow
npm run build
if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] TypeScript compilation failed" -ForegroundColor Red
    exit 1
}
Write-Host ""

# 4. Verify output
if (-not (Test-Path "dist\index.js")) {
    Write-Host "[ERROR] Build output dist\index.js not found" -ForegroundColor Red
    exit 1
}
Write-Host "[Step 4] Build output verified" -ForegroundColor Green
Write-Host ""

# 5. Verify logo image
if (-not (Test-Path "images\logo.png")) {
    Write-Host "[ERROR] images\logo.png not found. Run create-logo.ps1 first." -ForegroundColor Red
    exit 1
}
Write-Host "[Step 4.1] Logo image verified" -ForegroundColor Green
Write-Host ""

# 6. Clean old packages
if (Test-Path "eext-batch-led-place.eext") { Remove-Item "eext-batch-led-place.eext" -Force }
if (Test-Path "eext-batch-led-place.zip")  { Remove-Item "eext-batch-led-place.zip" -Force }

# 7. Create ZIP package (using .NET ZipFile to ensure forward-slash paths)
Write-Host "[Step 5] Packaging .eext file..." -ForegroundColor Yellow
Write-Host "  Included files:" -ForegroundColor Gray
Write-Host "    extension.json" -ForegroundColor Gray
Write-Host "    dist/" -ForegroundColor Gray
Write-Host "    images/" -ForegroundColor Gray
Write-Host "    README.md" -ForegroundColor Gray
Write-Host ""

Add-Type -AssemblyName System.IO.Compression.FileSystem

$zipPath = Join-Path $ProjectDir "eext-batch-led-place.zip"
$zip = [System.IO.Compression.ZipFile]::Open($zipPath, 'Create')

$filesToPack = @(
    "extension.json",
    "README.md"
)

$dirsToPack = @(
    "dist",
    "images"
)

foreach ($f in $filesToPack) {
    $srcPath = Join-Path $ProjectDir $f
    # Use forward slash for ZIP entry name
    $entryName = $f -replace '\\', '/'
    [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $srcPath, $entryName, 'Optimal') | Out-Null
    Write-Host "  Added: $entryName" -ForegroundColor DarkGray
}

foreach ($d in $dirsToPack) {
    $dirPath = Join-Path $ProjectDir $d
    $items = Get-ChildItem -Path $dirPath -File -Recurse
    foreach ($item in $items) {
        $relativePath = $item.FullName.Substring($ProjectDir.Length + 1)
        # Use forward slash for ZIP entry name
        $entryName = $relativePath -replace '\\', '/'
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $item.FullName, $entryName, 'Optimal') | Out-Null
        Write-Host "  Added: $entryName" -ForegroundColor DarkGray
    }
}

$zip.Dispose()

if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] Archive creation failed" -ForegroundColor Red
    exit 1
}

# 8. Rename to .eext
Rename-Item "eext-batch-led-place.zip" "eext-batch-led-place.eext"

if (-not (Test-Path "eext-batch-led-place.eext")) {
    Write-Host "[ERROR] Failed to create .eext file" -ForegroundColor Red
    exit 1
}

$fileSize = (Get-Item "eext-batch-led-place.eext").Length
$fileSizeKB = [math]::Round($fileSize / 1024, 1)

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "  Build & Pack SUCCESS!" -ForegroundColor Green
Write-Host "  Output: $ProjectDir\eext-batch-led-place.eext ($fileSizeKB KB)" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "How to install in EDA:" -ForegroundColor Cyan
Write-Host "  1. Open LCEDA Pro" -ForegroundColor White
Write-Host "  2. Menu > Advanced > Extension Manager" -ForegroundColor White
Write-Host "  3. Click 'Import'" -ForegroundColor White
Write-Host "  4. Select eext-batch-led-place.eext" -ForegroundColor White
Write-Host "  5. After import, open a PCB file" -ForegroundColor White
Write-Host "  6. Look for '批量设置元件坐标' in the top menu bar" -ForegroundColor White
Write-Host ""
