@echo off
set PATH=C:\msys64\mingw64\bin;C:\msys64\usr\bin;%PATH%
ninja -C build
if %errorlevel% neq 0 exit /b %errorlevel%

if not exist bin mkdir bin
echo Copying files to bin...
copy /Y build\qemu-system-arm.exe bin\
copy /Y build\qemu-img.exe bin\
if exist zImage_h copy /Y zImage_h bin\
if exist system_h.dtb copy /Y system_h.dtb bin\
