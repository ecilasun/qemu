@echo off
set PATH=C:\msys64\mingw64\bin;C:\msys64\usr\bin;%PATH%
cd bin
qemu-system-arm.exe -M sandpiper -m 512 -serial tcp::5555,server,nowait -drive file=extracted_sdcard.img,if=sd,format=raw -kernel zImage_h -dtb system_h.dtb -append "console=ttyPS0,115200 root=/dev/mmcblk0p2 rw rootwait earlyprintk" -net nic -net user,hostfwd=tcp::2222-:22
cd ..
pause
