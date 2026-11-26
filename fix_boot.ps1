param()
$ErrorActionPreference = "Stop"

Write-Host "=== Pulzar USB Fix ===" -ForegroundColor Cyan

# 1. Trouve Z:\ ou demande de rebrancher
if (-not (Test-Path 'Z:\')) {
    Write-Error "Z: introuvable — rebranche la cle USB et relance."
    exit 1
}

$partition = Get-Partition | Where-Object { $_.DriveLetter -eq 'Z' }
$diskNum = $partition.DiskNumber
if ($diskNum -eq 0) { Write-Error "STOP: diskNum=0 = disque systeme."; exit 1 }

Write-Host "[1/4] Disque USB = $diskNum — OK" -ForegroundColor Yellow

# 2. Backup propre
$backupDir = $env:TEMP + '\pulzar_fix_backup'
New-Item -ItemType Directory -Path $backupDir -Force | Out-Null
$toBackup = @('EFI\BOOT\BOOTX64.EFI','EFI\Limine\limine_x64.efi','EFI\pulzar\hv-core.efi','limine.conf')
foreach ($f in $toBackup) {
    $src = 'Z:\' + $f
    if (Test-Path $src) {
        $dst = $backupDir + '\' + ($f -replace '\\','_')
        Copy-Item $src $dst -Force
        Write-Host "    Backup: $f"
    }
}
Write-Host "[2/4] Backup dans $backupDir — OK" -ForegroundColor Yellow

# 3. Source hv-core
$hvCore = "C:\Users\PC\Downloads\pulzar-hypervisor-main\pulzar-hypervisor-main\target\x86_64-unknown-uefi\debug\hv-core.efi"
if (-not (Test-Path $hvCore)) {
    Write-Error "hv-core.efi introuvable: $hvCore"
    exit 1
}

Write-Host "[3/4] Deploiement hv-core.efi..." -ForegroundColor Yellow

# Assure la structure
New-Item -ItemType Directory -Path 'Z:\EFI\BOOT'   -Force | Out-Null
New-Item -ItemType Directory -Path 'Z:\EFI\Limine' -Force | Out-Null
New-Item -ItemType Directory -Path 'Z:\EFI\pulzar' -Force | Out-Null

# hv-core en BOOTX64.EFI (entrypoint UEFI direct, pas de Limine)
Copy-Item $hvCore 'Z:\EFI\BOOT\BOOTX64.EFI'   -Force
Copy-Item $hvCore 'Z:\EFI\pulzar\hv-core.efi' -Force

# Limine reste en place mais n'est plus l'entrypoint
if (Test-Path ($backupDir + '\EFI_Limine_limine_x64.efi')) {
    Copy-Item ($backupDir + '\EFI_Limine_limine_x64.efi') 'Z:\EFI\Limine\limine_x64.efi' -Force
}

# limine.conf mis a jour (au cas ou Limine serait quand meme charge)
$conf = "timeout: 3`r`nserial: no`r`n`r`n/Pulzar Hypervisor`r`n    protocol: chainload`r`n    image_path: boot():/EFI/pulzar/hv-core.efi"
[System.IO.File]::WriteAllText('Z:\limine.conf', $conf, [System.Text.Encoding]::ASCII)

Write-Host "[4/4] Verification finale..." -ForegroundColor Yellow

$boot   = Get-Item 'Z:\EFI\BOOT\BOOTX64.EFI'
$core   = Get-Item 'Z:\EFI\pulzar\hv-core.efi'
$src    = Get-Item $hvCore

Write-Host ""
Write-Host "=== Structure Z:\ ===" -ForegroundColor Cyan
Get-ChildItem -Path 'Z:\' -Recurse | Select-Object FullName, Length | Format-Table -AutoSize

Write-Host ""
if ($boot.Length -eq $src.Length -and $core.Length -eq $src.Length) {
    Write-Host "=== BOOTX64.EFI = hv-core.efi ($($src.Length) bytes) ===" -ForegroundColor Green
    Write-Host "=== DONE — Reboot et selectionne la cle USB ===" -ForegroundColor Green
} else {
    Write-Host "WARN: tailles incoherentes — verifie manuellement" -ForegroundColor Red
    Write-Host "  BOOTX64.EFI : $($boot.Length)"
    Write-Host "  hv-core.efi : $($core.Length)"
    Write-Host "  source      : $($src.Length)"
}
