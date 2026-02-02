#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Extract files from a qcow2 image on Windows
.DESCRIPTION
    This script extracts files from a qcow2 disk image by converting to raw format,
    mounting partitions, and extracting specified files. Optionally extracts zImage
    from image.ub and converts dtb to dts.
.PARAMETER QcowImage
    Path to the qcow2 image file
.PARAMETER OutputDir
    Directory to save extracted files (default: current directory)
.PARAMETER PartitionNumber
    Partition number to mount (default: 1 for BOOT partition)
.PARAMETER ExtractZImage
    Extract zImage from image.ub using extract_zimage.py
.PARAMETER ConvertDtb
    Convert system.dtb to system.dts (requires dtc.exe in PATH)
.PARAMETER KeepRaw
    Keep the temporary raw image file after extraction
.EXAMPLE
    .\extract_from_qcow2.ps1 -QcowImage "disk.qcow2" -OutputDir "extracted" -ExtractZImage
#>

param(
    [Parameter(Mandatory=$true)]
    [string]$QcowImage,
    
    [Parameter(Mandatory=$false)]
    [string]$OutputDir = ".",
    
    [Parameter(Mandatory=$false)]
    [int]$PartitionNumber = 1,
    
    [Parameter(Mandatory=$false)]
    [switch]$ExtractZImage,
    
    [Parameter(Mandatory=$false)]
    [switch]$ConvertDtb,
    
    [Parameter(Mandatory=$false)]
    [switch]$KeepRaw,
    
    [Parameter(Mandatory=$false)]
    [string[]]$FilesToExtract = @("image.ub", "image_h.ub", "system.dtb", "system.dts")
)

# Error handling
$ErrorActionPreference = "Stop"

# Resolve paths
$QcowImage = Resolve-Path $QcowImage -ErrorAction Stop
$OutputDir = if (Test-Path $OutputDir) { Resolve-Path $OutputDir } else { 
    New-Item -ItemType Directory -Path $OutputDir -Force | Select-Object -ExpandProperty FullName 
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir

Write-Host "===== QCOW2 File Extractor =====" -ForegroundColor Cyan
Write-Host "QCOW2 Image: $QcowImage"
Write-Host "Output Directory: $OutputDir"
Write-Host ""

# Find qemu-img
$qemuImg = $null
$possiblePaths = @(
    "qemu-img.exe",
    ".\qemu-img.exe",
    "$repoRoot\qemu-img.exe",
    "$repoRoot\build\qemu-img.exe",
    "$env:ProgramFiles\qemu\qemu-img.exe"
)

foreach ($path in $possiblePaths) {
    $resolved = Get-Command $path -ErrorAction SilentlyContinue
    if ($resolved) {
        $qemuImg = $resolved.Source
        break
    }
}

if (-not $qemuImg) {
    Write-Error "qemu-img.exe not found. Please ensure QEMU is installed and in PATH."
    exit 1
}

Write-Host "Using qemu-img: $qemuImg" -ForegroundColor Green

# Create temporary raw image
$rawImage = [System.IO.Path]::Combine($env:TEMP, "qcow2_extract_$(Get-Date -Format 'yyyyMMdd_HHmmss').raw")
Write-Host ""
Write-Host "Step 1: Converting qcow2 to raw format..." -ForegroundColor Yellow
Write-Host "Temporary file: $rawImage"

try {
    & $qemuImg convert -f qcow2 -O raw "$QcowImage" "$rawImage"
    if ($LASTEXITCODE -ne 0) {
        throw "qemu-img conversion failed with exit code $LASTEXITCODE"
    }
    Write-Host "Conversion completed successfully!" -ForegroundColor Green
} catch {
    Write-Error "Failed to convert qcow2 to raw: $_"
    exit 1
}

# Mount the raw image
Write-Host ""
Write-Host "Step 2: Mounting raw image..." -ForegroundColor Yellow

try {
    # Mount the disk image
    $mountResult = Mount-DiskImage -ImagePath $rawImage -StorageType RAW -Access ReadOnly -PassThru
    Start-Sleep -Seconds 2
    
    # Get the disk number
    $disk = Get-DiskImage -ImagePath $rawImage | Get-Disk
    $diskNumber = $disk.Number
    Write-Host "Disk mounted as Disk $diskNumber" -ForegroundColor Green
    
    # Get partitions
    $partitions = Get-Partition -DiskNumber $diskNumber
    Write-Host ""
    Write-Host "Available partitions:" -ForegroundColor Cyan
    $partitions | ForEach-Object {
        $driveLetter = if ($_.DriveLetter) { "$($_.DriveLetter):" } else { "No drive letter" }
        Write-Host "  Partition $($_.PartitionNumber): $driveLetter - Size: $([math]::Round($_.Size/1MB, 2)) MB"
    }
    
    # Select partition
    $partition = $partitions | Where-Object { $_.PartitionNumber -eq $PartitionNumber } | Select-Object -First 1
    
    if (-not $partition) {
        throw "Partition $PartitionNumber not found"
    }
    
    # Assign drive letter if needed
    $driveLetter = $partition.DriveLetter
    if (-not $driveLetter) {
        Write-Host "Assigning drive letter to partition $PartitionNumber..." -ForegroundColor Yellow
        $partition | Set-Partition -NewDriveLetter (Get-AvailableDriveLetter)
        Start-Sleep -Seconds 1
        $partition = Get-Partition -DiskNumber $diskNumber -PartitionNumber $PartitionNumber
        $driveLetter = $partition.DriveLetter
    }
    
    $mountPoint = "${driveLetter}:\"
    Write-Host "Partition $PartitionNumber mounted at: $mountPoint" -ForegroundColor Green
    
    # Extract files
    Write-Host ""
    Write-Host "Step 3: Extracting files..." -ForegroundColor Yellow
    
    $extractedFiles = @()
    
    foreach ($fileName in $FilesToExtract) {
        $sourcePath = Join-Path $mountPoint $fileName
        $destPath = Join-Path $OutputDir $fileName
        
        if (Test-Path $sourcePath) {
            Write-Host "  Extracting: $fileName" -ForegroundColor Cyan
            Copy-Item -Path $sourcePath -Destination $destPath -Force
            $extractedFiles += @{
                Name = $fileName
                Path = $destPath
            }
            Write-Host "    Saved to: $destPath" -ForegroundColor Green
        } else {
            Write-Host "  Skipping: $fileName (not found)" -ForegroundColor Gray
        }
    }
    
    # List all files in mount point for reference
    Write-Host ""
    Write-Host "All files in partition:" -ForegroundColor Cyan
    Get-ChildItem -Path $mountPoint -Recurse -File -ErrorAction SilentlyContinue | 
        Select-Object -First 20 | 
        ForEach-Object { Write-Host "  $($_.FullName.Replace($mountPoint, '/'))" -ForegroundColor Gray }
    
    Write-Host ""
    Write-Host "Extraction completed! Files saved to: $OutputDir" -ForegroundColor Green
    
} catch {
    Write-Error "Error during mounting/extraction: $_"
} finally {
    # Unmount
    Write-Host ""
    Write-Host "Step 4: Cleaning up..." -ForegroundColor Yellow
    
    try {
        Dismount-DiskImage -ImagePath $rawImage -ErrorAction SilentlyContinue
        Write-Host "Disk image dismounted" -ForegroundColor Green
    } catch {
        Write-Warning "Failed to dismount disk image: $_"
    }
    
    # Remove temporary raw file
    if (-not $KeepRaw) {
        if (Test-Path $rawImage) {
            Start-Sleep -Seconds 2
            Remove-Item $rawImage -Force -ErrorAction SilentlyContinue
            Write-Host "Temporary raw image removed" -ForegroundColor Green
        }
    } else {
        Write-Host "Raw image kept at: $rawImage" -ForegroundColor Cyan
    }
}

# Extract zImage from image.ub
if ($ExtractZImage -and (Test-Path (Join-Path $OutputDir "image.ub"))) {
    Write-Host ""
    Write-Host "Step 5: Extracting zImage from image.ub..." -ForegroundColor Yellow
    
    $pythonCmd = Get-Command python -ErrorAction SilentlyContinue
    if (-not $pythonCmd) {
        $pythonCmd = Get-Command python3 -ErrorAction SilentlyContinue
    }
    
    if ($pythonCmd) {
        $extractScript = Join-Path $scriptDir "extract_zimage.py"
        $imageUb = Join-Path $OutputDir "image.ub"
        $zImageOut = Join-Path $OutputDir "zImage"
        
        if (Test-Path $extractScript) {
            & $pythonCmd.Source $extractScript $imageUb $zImageOut
            if ($LASTEXITCODE -eq 0 -and (Test-Path $zImageOut)) {
                Write-Host "zImage extracted successfully: $zImageOut" -ForegroundColor Green
            } else {
                Write-Warning "Failed to extract zImage from image.ub"
            }
        } else {
            Write-Warning "extract_zimage.py not found at: $extractScript"
        }
    } else {
        Write-Warning "Python not found. Cannot extract zImage. Install Python and try again."
    }
}

# Convert DTB to DTS
if ($ConvertDtb -and (Test-Path (Join-Path $OutputDir "system.dtb"))) {
    Write-Host ""
    Write-Host "Step 6: Converting DTB to DTS..." -ForegroundColor Yellow
    
    $dtc = Get-Command dtc -ErrorAction SilentlyContinue
    if ($dtc) {
        $dtbFile = Join-Path $OutputDir "system.dtb"
        $dtsFile = Join-Path $OutputDir "system.dts"
        
        & dtc -I dtb -O dts -o $dtsFile $dtbFile
        if ($LASTEXITCODE -eq 0 -and (Test-Path $dtsFile)) {
            Write-Host "Device tree source created: $dtsFile" -ForegroundColor Green
        } else {
            Write-Warning "Failed to convert DTB to DTS"
        }
    } else {
        Write-Warning "dtc (Device Tree Compiler) not found in PATH. Cannot convert DTB to DTS."
        Write-Host "  Install dtc or manually convert with: dtc -I dtb -O dts -o system.dts system.dtb"
    }
}

# Summary
Write-Host ""
Write-Host "===== Extraction Complete =====" -ForegroundColor Cyan
Write-Host "Output directory: $OutputDir"
Write-Host ""
Write-Host "Extracted files:" -ForegroundColor Green
Get-ChildItem -Path $OutputDir | ForEach-Object {
    $size = if ($_.Length -gt 1MB) { 
        "$([math]::Round($_.Length/1MB, 2)) MB" 
    } else { 
        "$([math]::Round($_.Length/1KB, 2)) KB" 
    }
    Write-Host "  $($_.Name) - $size"
}

Write-Host ""
Write-Host "Done!" -ForegroundColor Green

# Helper function to get available drive letter
function Get-AvailableDriveLetter {
    $usedLetters = Get-PSDrive -PSProvider FileSystem | Select-Object -ExpandProperty Name
    $letters = 69..90 | ForEach-Object { [char]$_ } # E-Z
    foreach ($letter in $letters) {
        if ($letter -notin $usedLetters) {
            return $letter
        }
    }
    throw "No available drive letters"
}
