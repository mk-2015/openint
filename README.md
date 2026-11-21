# OpenINT (oint)

- Written primarily in the `C` Programming Language, is a minimal init system for linux.

## Build layout

Projects may use any directory name under `inner/`; a directory is included
only when it contains a `.project` file. That file declares its type with
exactly one value: `lib`, `tool`, or `init`.

- A `lib` project is built with its own Makefile and must produce `lib/` and
  `include/` directories.
- A `tool` project is built with its own Makefile and must produce `bin/`.
- There must be exactly one `init` project. It must produce exactly one
  executable, `bin/init`.

Run `make` to build all libraries, then all tools, then init. Outputs are copied
into `out/lib/`, `out/include/`, and `out/bin/`, respectively. Libraries are
available before tools are built, and tools and libraries are available before
init is built. The output include and library directories are exported as
compiler search paths to nested builds. Init also receives `EINC`, containing
the include path, library search path, and `-l` flags derived from copied
`lib*.a` and `lib*.so` files.

The bundled init project's Makefile expects C sources under its `src/`
directory. New projects can use any name, as long as they provide a Makefile
and the expected output directories for their `.project` type.

The init logger is exposed from `init/log.h`: `log_message(level, format, ...)`
supports debug, info, warning, and error levels, while `set_filter(level)`
sets the minimum displayed level. `load(&done, format, ...)` shows a spinner
until the `volatile sig_atomic_t` flag `done` becomes `1`.

## System images

The optional `rootfs/` directory is overlaid on the staged filesystem.
`make initramfs` uses `mkinitramfs` when available (then dracut or mkinitcpio)
to create `build/initramfs.img`; it does not archive the staged disk rootfs.
Set `KERNEL_RELEASE` to select the kernel version (defaults to the running
kernel), or set `INITRAMFS_TOOL` to choose a tool explicitly. At least one of
these tools must be installed.
`make vda` creates `build/oint.vda`, a raw ext4 filesystem image; set `VDA_SIZE`
or `VDA_IMAGE` to customize it. This target requires `mkfs.ext4`.

The VDA target packages the staged rootfs directory as a raw ext4 disk image,
not a partition table or bootloader. The init executable is installed at `/init`
and `/sbin/init` in this disk rootfs. The staged rootfs includes the `/dev`,
`/proc`, `/sys`, and `/run` mountpoints needed during initramfs switch-root.
Booting it as a VM disk still requires a kernel and a VM setup that can boot the
ext4 filesystem on `/dev/vda`.
Dynamically linked programs also need their runtime loader and libraries
supplied through the `rootfs/` overlay.

Run `make -f build.mk` to build the QEMU bundle under `qemu/`: the staged
`qemu/rootfs/`, generated `qemu/initramfs.img`, and `qemu/disk.vda`.
The individual `rootfs`, `initramfs`, and `disk.vda` targets can also be
requested separately.

Run `make -f build.mk run` to boot the kernel at
`/boot/vmlinuz-6.12.117+deb13-amd64` with QEMU, the generated initramfs, and
`qemu/disk.vda` as `/dev/vda`. Override `KERNEL_IMAGE`, `KERNEL_RELEASE`,
`QEMU`, or `QEMU_MEMORY` when needed. This requires `qemu-system-x86_64`.