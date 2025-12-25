@echo off
set PATH=C:\msys64\mingw64\bin;C:\msys64\usr\bin;%PATH%
qemu-system-arm.exe -M sandpiper -m 1024 -serial tcp::5555,server,nowait -drive file=sdcard.qcow2,if=sd,format=qcow2 -kernel zImage_h -dtb system_h.dtb -append "console=ttyPS0,115200 console=tty1 root=/dev/mmcblk0p2 rw rootwait earlyprintk" -net nic -net user,hostfwd=tcp::2222-:22
pause
