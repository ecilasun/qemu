./qemu-system-arm-unsigned \
	-M sandpiper \
	-m 512M \
	-serial tcp::5555,server,nowait \
	-drive file=sdcard.qcow2,if=sd,format=qcow2 \
	-kernel zImage_h \
	-dtb system_h.dtb \
	-append "console=ttyPS0,115200 root=/dev/mmcblk0p2 earlyprintk" \
	-net nic \
	-net user,hostfwd=tcp::2222-:22 \
	-device usb-kbd,bus=usb-bus.1
  