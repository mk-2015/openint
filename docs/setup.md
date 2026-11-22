# Compilation and Setup

OpenINT uses GNU Make and expects a Linux build environment.

## Requirements

- GCC
- GNU Make
- POSIX build tools
- Static C libraries and headers needed by nested projects
- `mkfs.ext4` and `truncate` for disk images
- `qemu-system-x86_64` and a host kernel image for VM testing

On Debian-based systems, install the main build tools with:

```sh
sudo apt install build-essential e2tools e2fsprogs
```

Install QEMU separately if VM testing is needed:

```sh
sudo apt install qemu-system-x86_64
```

## Build

From the repository root:

```sh
make
```

The build discovers projects under `inner/` from their `.project` files. It
builds libraries, tools, and the init project in that order, then copies
outputs under `out/`.

The init project must provide a single executable at `inner/init/bin/init`.

## Disk and VM Outputs

Build or rebuild the raw ext4 disk image:

```sh
make vda
```

Build the initramfs:

```sh
make initramfs
```

Build the complete QEMU bundle:

```sh
make qemu-all
```

Boot with the configured kernel:

```sh
make run
```

Override the kernel or QEMU settings on the command line, for example:

```sh
make run KERNEL_IMAGE=/boot/vmlinuz KERNEL_RELEASE=6.12.0 QEMU_MEMORY=512M
```
