$outFile = "extracted_sdcard.img"
$qemuImg = "$PSScriptRoot\qemu-img.exe"
if (Test-Path $qemuImg) {
	Write-Host "Resizing image (+1M) to fix partition geometry..."
	# Ensure DLLs are found
	$env:PATH = "C:\msys64\mingw64\bin;$msysPath;" + $env:PATH
	& $qemuImg resize -f raw $outFile +1M
} else {
	Write-Warning "qemu-img.exe not found in script directory. Skipping resize."
}
