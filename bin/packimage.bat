@echo off
set PATH=C:\msys64\mingw64\bin;C:\msys64\usr\bin;%PATH%
"%~dp0qemu-img.exe" convert -f raw -O qcow2 -c "%~dp0extracted_sdcard.img" "%~dp0sdcard.qcow2"