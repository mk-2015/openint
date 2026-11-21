.DEFAULT_GOAL := all

QEMU_DIR ?= qemu
ROOTFS_STAGE ?= $(QEMU_DIR)/rootfs
INITRAMFS_IMAGE ?= $(QEMU_DIR)/initramfs.img
VDA_IMAGE ?= $(QEMU_DIR)/disk.vda
VDA_SIZE ?= 1G
DRACUT ?= dracut
MKINITRAMFS ?= mkinitramfs
MKINITCPIO ?= mkinitcpio
INITRAMFS_TOOL ?= auto
KERNEL_IMAGE ?= /boot/vmlinuz-6.12.117+deb13-amd64
KERNEL_RELEASE ?= 6.12.117+deb13-amd64
QEMU ?= qemu-system-x86_64
QEMU_MEMORY ?= 1G

.PHONY: all rootfs initramfs disk.vda run clean

all: rootfs initramfs disk.vda

rootfs:
	$(MAKE) stage-rootfs ROOTFS_STAGE="$(ROOTFS_STAGE)"

initramfs:
	$(MAKE) initramfs \
		INITRAMFS_IMAGE="$(INITRAMFS_IMAGE)" \
		KERNEL_RELEASE="$(KERNEL_RELEASE)" \
		INITRAMFS_TOOL="$(INITRAMFS_TOOL)" \
		MKINITRAMFS="$(MKINITRAMFS)" \
		DRACUT="$(DRACUT)" MKINITCPIO="$(MKINITCPIO)"

disk.vda: rootfs
	@mkdir -p "$(dir $(VDA_IMAGE))"
	@test -x "$(ROOTFS_STAGE)/init" || { echo "The disk rootfs must contain executable /init." >&2; exit 1; }
	truncate -s "$(VDA_SIZE)" "$(VDA_IMAGE)"
	mkfs.ext4 -F -d "$(ROOTFS_STAGE)" "$(VDA_IMAGE)"

run: all
	@test -r "$(KERNEL_IMAGE)" || { echo "Kernel image not found: $(KERNEL_IMAGE)" >&2; exit 1; }
	$(QEMU) -m "$(QEMU_MEMORY)" \
		-kernel "$(KERNEL_IMAGE)" \
		-initrd "$(INITRAMFS_IMAGE)" \
		-drive "file=$(VDA_IMAGE),format=raw,if=virtio" \
		-append "root=/dev/vda rootfstype=ext4 rw init=/init console=ttyS0" \
		-no-reboot -nographic

clean:
	$(MAKE) clean ROOTFS_STAGE="$(ROOTFS_STAGE)" INITRAMFS_IMAGE="$(INITRAMFS_IMAGE)" VDA_IMAGE="$(VDA_IMAGE)"
