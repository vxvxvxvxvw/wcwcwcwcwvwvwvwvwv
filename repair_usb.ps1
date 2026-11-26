param()
$ErrorActionPreference = "Stop"

Write-Host "=== Pulzar USB Repair ===" -ForegroundColor Cyan

Write-Host "[1/6] Identification du disque USB..."
$partition = Get-Partition | Where-Object { $_.DriveLetter -eq 'Z' }
if (-not $partition) {
    Write-Error "Volume Z: introuvable."
    exit 1
}
$diskNum = $partition.DiskNumber
$disk = Get-Disk -Number $diskNum
Write-Host "    Disque $diskNum" -ForegroundColor Yellow

if ($diskNum -eq 0) {
    Write-Error "STOP: diskNum=0 = disque systeme. Abandon."
    exit 1
}

$backupDir = $env:TEMP + '\pulzar_backup'
New-Item -ItemType Directory -Path $backupDir -Force | Out-Null
Write-Host "[2/6] Sauvegarde vers $backupDir ..."

$filesToBackup = @(
    'EFI\BOOT\BOOTX64.EFI',
    'EFI\Limine\limine_x64.efi',
    'EFI\pulzar\hv-core.efi',
    'pulzar.efi'
)

foreach ($f in $filesToBackup) {
    $src = 'Z:\' + $f
    if (Test-Path $src) {
        $dst = $backupDir + '\' + $f
        $dstDir = Split-Path $dst -Parent
        New-Item -ItemType Directory -Path $dstDir -Force | Out-Null
        Copy-Item $src $dst -Force
        Write-Host "    OK: $f"
    } else {
        Write-Host "    MANQUANT: $f" -ForegroundColor Red
    }
}

Write-Host "[3/6] Formatage FAT32 du disque $diskNum ..."
$dpFile = $env:TEMP + '\dp_repair.txt'
$partStyle = $disk.PartitionStyle
Write-Host "    Style actuel: $partStyle"

if ($partStyle -eq 'GPT') {
    $dpContent = "select disk $diskNum`r`nclean`r`ncreate partition primary`r`nformat fs=fat32 quick label=PULZAR`r`nassign letter=Z`r`nexit"
} else {
    $dpContent = "select disk $diskNum`r`nclean`r`nconvert gpt`r`ncreate partition primary`r`nformat fs=fat32 quick label=PULZAR`r`nassign letter=Z`r`nexit"
}

[System.IO.File]::WriteAllText($dpFile, $dpContent, [System.Text.Encoding]::ASCII)
diskpart /s $dpFile
Start-Sleep -Seconds 4

if (-not (Test-Path 'Z:\')) {
    Write-Error "Z: non remonte apres format."
    exit 1
}
Write-Host "    Z: formatee OK" -ForegroundColor Green

Write-Host "[4/6] Creation structure EFI..."
New-Item -ItemType Directory -Path 'Z:\EFI\BOOT' -Force | Out-Null
New-Item -ItemType Directory -Path 'Z:\EFI\Limine' -Force | Out-Null
New-Item -ItemType Directory -Path 'Z:\EFI\pulzar' -Force | Out-Null

Write-Host "[5/6] Restauration des fichiers EFI..."
foreach ($f in $filesToBackup) {
    $src = $backupDir + '\' + $f
    $dst = 'Z:\' + $f
    if (Test-Path $src) {
        $dstDir = Split-Path $dst -Parent
        New-Item -ItemType Directory -Path $dstDir -Force | Out-Null
        Copy-Item $src $dst -Force
        Write-Host "    Restaure: $f"
    } else {
        Write-Host "    SKIP: $f" -ForegroundColor Yellow
    }
}

Write-Host "[6/6] Ecriture limine.conf..."
$conf = "timeout: 3`r`nserial: no`r`n`r`n/Pulzar Hypervisor`r`n    protocol: chainload`r`n    image_path: boot():/EFI/pulzar/hv-core.efi"
[System.IO.File]::WriteAllText('Z:\limine.conf', $conf, [System.Text.Encoding]::ASCII)

Write-Host ""
Write-Host "=== Structure finale Z:\ ===" -ForegroundColor Cyan
Get-ChildItem -Path 'Z:\' -Recurse | Select-Object FullName, Length | Format-Table -AutoSize

Write-Host ""
Write-Host "=== DONE ===" -ForegroundColor Green
Write-Host "Redemarre et selectionne la cle USB dans le BIOS." -ForegroundColor Green
