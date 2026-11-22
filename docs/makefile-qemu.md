# QEMU Examples

The root Makefile provides targets for building and running OpenINT in QEMU.

## One-Command Boot

```sh
make run
```

This requires a readable host kernel and builds the initramfs and disk image
before starting QEMU.

The default QEMU disk and kernel arguments are:

```text
root=/dev/vda rootfstype=ext4 rw init=/init console=tty0
```

## Individual Targets

```sh
# Stage the disk root filesystem
make stage-rootfs

# Build the raw ext4 disk
make vda

# Build the initramfs
make initramfs

# Build both images
make qemu-all
```

## Common Overrides

```sh
make run KERNEL_IMAGE=/boot/vmlinuz-$(uname -r)
make run QEMU_MEMORY=512M
make run QEMU_EXTRA_ARGS="-nographic"
make run VDA_SIZE=2G
```

Set `QEMU` to use a different QEMU executable:

```sh
make run QEMU=qemu-system-aarch64
```
