# How to Use

The OpenINT init executable must run as PID 1. The normal entry point is
`/init` in the staged disk filesystem.

## Normal Boot

Boot the disk image with `/init` as the init process:

```text
init=/init
```

OpenINT mounts the virtual filesystems, initializes the console, sets the
hostname, and starts its local IPC server at `/run/oinit.sock`.

## Rescue Mode

Pass `--rescue` to enter the rescue shell:

```text
init=/init -- --rescue
```

In rescue mode, OpenINT enters its built-in rescue interface instead of
starting the normal environment.

## Build Artifacts

After `make`:

- `out/bin/init` is the init executable.
- `out/lib/` contains built libraries.
- `out/include/` contains exported headers.
- `qemu/rootfs/` is the staged disk root filesystem.
- `qemu/disk.vda` is the raw ext4 disk image.
- `qemu/initramfs.img` is the generated initramfs.
