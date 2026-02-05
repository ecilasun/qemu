Open mingw64 terminal and run:

```
pacman -Syu --noconfirm
```

Close and restart terminal then run:

```
pacman -Su --noconfirm
```

Then install dependencies with:

```
pacman -S --noconfirm base-devel mingw-w64-x86_64-toolchain git python ninja mingw-w64-x86_64-glib2 mingw-w64-x86_64-gtk3 mingw-w64-x86_64-SDL2 mingw-w64-x86_64-libslirp mingw-w64-x86_64-pixman mingw-w64-x86_64-libssh mingw-w64-x86_64-libnfs
```

After this go to qemu root folder and use this to configure:

```
./configure --target-list=arm-softmmu --enable-slirp --disable-werror
```

Now we can build with the appropriate script depending on host platform:

```
For MacOS / Linux (autodetects platform)
./build.sh
or for Windows, use:
./build.bat
```
