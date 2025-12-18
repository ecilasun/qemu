# Check for Administrator privileges
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole] "Administrator")) {
    Write-Warning "You must run this script as Administrator to access physical drives."
    Write-Warning "Please open a new PowerShell window as Administrator and run this script again."
    exit
}

# Configuration
$driveLetter = "H"
$msysPath = "C:\msys64\usr\bin"
$ddPath = "$msysPath\dd.exe"
$outFile = "extracted_sdcard.img"

# Find the physical disk
try {
    $disk = Get-Partition -DriveLetter $driveLetter -ErrorAction Stop | Get-Disk
} catch {
    Write-Error "Could not find a physical disk corresponding to drive $driveLetter. Is the SD card inserted?"
    exit
}

$diskNum = $disk.Number
Write-Host "Found Drive $driveLetter on PhysicalDisk$diskNum"
Write-Host "Disk Model: $($disk.FriendlyName)"
Write-Host "Disk Size: $([math]::Round($disk.Size / 1GB, 2)) GB"

# Check for dd
if (-not (Test-Path $ddPath)) {
    Write-Error "dd.exe not found at $ddPath. Please ensure MSYS2 is installed."
    exit
}

# Confirm with user
Write-Host ""
Write-Host "WARNING: This will read the ENTIRE content of PhysicalDisk$diskNum and save it to $outFile."
Write-Host "This may take a while depending on the SD card speed."
Write-Host ""
# In an automated script we might skip confirmation, but for safety let's print what we are doing.

# Run dd
# We use the device path \\.\PhysicalDriveN
$devicePath = "\\.\PhysicalDrive$diskNum"

Write-Host "Starting extraction..."
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $ddPath
$psi.Arguments = "if=$devicePath of=$outFile bs=1M status=progress"
$psi.UseShellExecute = $false
$psi.RedirectStandardError = $false # Let dd print progress to console
$process = [System.Diagnostics.Process]::Start($psi)
$process.WaitForExit()

if ($process.ExitCode -eq 0) {
    Write-Host "Extraction complete!"

    # Resize image to fix potential geometry issues (partition table exceeding disk size)
    $qemuImg = "$PSScriptRoot\qemu-img.exe"
    if (Test-Path $qemuImg) {
        Write-Host "Resizing image (+1M) to fix partition geometry..."
        # Ensure DLLs are found
        $env:PATH = "C:\msys64\mingw64\bin;$msysPath;" + $env:PATH
        & $qemuImg resize -f raw $outFile +1M
    } else {
        Write-Warning "qemu-img.exe not found in script directory. Skipping resize."
    }
} else {
    Write-Error "Extraction failed with exit code $($process.ExitCode)"
}
