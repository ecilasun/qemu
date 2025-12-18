To extract the petalinux disk image, first run this from an admin PowerShell terminal:
```
PowerShell -ExecutionPolicy Bypass -File .\extract_sdcard.ps1
```

To extract the kernel image to zImage_h file, run:
```
extractkernel.bat
```

Follow by this to compact the resulting disk image:
```
packimage.bat
```
