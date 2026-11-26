$ErrorActionPreference = "Continue"
$root = "C:\Users\PC\Downloads\pulzar-hypervisor-main\pulzar-hypervisor-main"

Set-Location $root

Write-Host "=== CLEAN hv-loader ===" -ForegroundColor Cyan
cargo clean -p hv-loader 2>&1
Write-Host "=== CLEAN hv-core ===" -ForegroundColor Cyan
cargo clean -p hv-core 2>&1

Write-Host "=== BUILD hv-loader ===" -ForegroundColor Cyan
$loaderResult = cargo build -p hv-loader 2>&1
$loaderResult | Out-String | Write-Host
$loaderExit = $LASTEXITCODE
Write-Host "EXIT_LOADER=$loaderExit"

if ($loaderExit -eq 0) {
    Write-Host "=== BUILD hv-core ===" -ForegroundColor Cyan
    $coreResult = cargo build -p hv-core 2>&1
    $coreResult | Out-String | Write-Host
    $coreExit = $LASTEXITCODE
    Write-Host "EXIT_CORE=$coreExit"
} else {
    Write-Host "hv-loader failed, skipping hv-core" -ForegroundColor Yellow
    $coreExit = -1
}

Write-Host ""
Write-Host "=== RESULTS ===" -ForegroundColor Green
if ($loaderExit -eq 0) {
    Write-Host "hv-loader: OK" -ForegroundColor Green
    $loaderBin = Join-Path $root "target\x86_64-unknown-uefi\debug\hv-loader.efi"
    if (Test-Path $loaderBin) {
        $sz = (Get-Item $loaderBin).Length
        Write-Host "  -> $loaderBin ($sz bytes)"
    }
} else {
    Write-Host "hv-loader: FAILED (exit $loaderExit)" -ForegroundColor Red
}

if ($coreExit -eq 0) {
    Write-Host "hv-core: OK" -ForegroundColor Green
    $coreBin = Join-Path $root "target\x86_64-unknown-uefi\debug\hv-core.efi"
    if (Test-Path $coreBin) {
        $sz = (Get-Item $coreBin).Length
        Write-Host "  -> $coreBin ($sz bytes)"
    }
} elseif ($coreExit -ne -1) {
    Write-Host "hv-core: FAILED (exit $coreExit)" -ForegroundColor Red
}
