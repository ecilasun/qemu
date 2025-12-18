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

To run the emulator, first start it by using boot_emulator.bat, then once the login screen appears run terminal.bat and respond with your password. This starts an ssh session and you can use the emulator as if on a remote terminal, or debug your code.