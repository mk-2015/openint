.DEFAULT_GOAL := all

MAKE ?= make
INNER_DIR ?= inner
OUT_DIR ?= out
ROOTFS_DIR ?= rootfs

# Staging & Image Outputs
BUILD_DIR ?= qemu
ROOTFS_STAGE ?= $(BUILD_DIR)/rootfs
INITRAMFS_IMAGE ?= $(BUILD_DIR)/initramfs.img
VDA_IMAGE ?= $(BUILD_DIR)/disk.vda
VDA_SIZE ?= 1G

# Initramfs & Kernel Settings
DRACUT ?= dracut
MKINITRAMFS ?= mkinitramfs
MKINITCPIO ?= mkinitcpio
INITRAMFS_TOOL ?= auto
KERNEL_RELEASE ?= $(shell uname -r)
KERNEL_IMAGE ?= /boot/vmlinuz-$(KERNEL_RELEASE)

# QEMU Settings
QEMU ?= qemu-system-x86_64
QEMU_MEMORY ?= 1G
QEMU_APPEND ?= root=/dev/vda rootfstype=ext4 rw init=/init console=tty0 -- --rescue
QEMU_EXTRA_ARGS ?=

# Compiler / Project Discovery
PROJECT_OUT_DIR := $(abspath $(OUT_DIR))
EINC := -I$(PROJECT_OUT_DIR)/include -L$(PROJECT_OUT_DIR)/lib

export PROJECT_OUT_DIR
export CPATH := $(PROJECT_OUT_DIR)/include$(if $(CPATH),:$(CPATH))
export LIBRARY_PATH := $(PROJECT_OUT_DIR)/lib$(if $(LIBRARY_PATH),:$(LIBRARY_PATH))

PROJECT_MARKERS := $(wildcard $(INNER_DIR)/*/.project)
PROJECTS := $(patsubst %/.project,%,$(PROJECT_MARKERS))
project_type = $(strip $(file <$(1)/.project))
LIB_PROJECTS := $(foreach project,$(PROJECTS),$(if $(filter lib,$(call project_type,$(project))),$(project)))
TOOL_PROJECTS := $(foreach project,$(PROJECTS),$(if $(filter tool,$(call project_type,$(project))),$(project)))
INIT_PROJECTS := $(foreach project,$(PROJECTS),$(if $(filter init,$(call project_type,$(project))),$(project)))
INVALID_PROJECTS := $(foreach project,$(PROJECTS),$(if $(filter tool lib init,$(call project_type,$(project))),,$(project)))

.PHONY: all validate libraries tools init stage-rootfs initramfs vda disk.vda qemu-all run clean distclean

all: init

validate:
	@if [ "$(words $(INIT_PROJECTS))" -ne 1 ]; then \
		echo "Expected exactly one init project under $(INNER_DIR); found $(words $(INIT_PROJECTS))." >&2; \
		exit 1; \
	fi
	@if [ -n "$(strip $(INVALID_PROJECTS))" ]; then \
		echo "Unknown .project type in: $(INVALID_PROJECTS) (expected tool, lib, or init)." >&2; \
		exit 1; \
	fi

libraries: validate
	@mkdir -p "$(OUT_DIR)/bin" "$(OUT_DIR)/lib" "$(OUT_DIR)/include"
	@set -eu; \
	for project in $(LIB_PROJECTS); do \
		echo "[LIB] $$project"; \
		$(MAKE) -C "$$project"; \
		test -d "$$project/lib" || { echo "Library project $$project did not produce lib/." >&2; exit 1; }; \
		test -d "$$project/include" || { echo "Library project $$project is missing include/." >&2; exit 1; }; \
		cp -a "$$project/lib/." "$(OUT_DIR)/lib/"; \
		cp -a "$$project/include/." "$(OUT_DIR)/include/"; \
	done

tools: libraries
	@set -eu; \
	for project in $(TOOL_PROJECTS); do \
		echo "[TOOL] $$project"; \
		$(MAKE) -C "$$project"; \
		test -d "$$project/bin" || { echo "Tool project $$project did not produce bin/." >&2; exit 1; }; \
		if [ -e "$$project/bin/init" ]; then \
			echo "Tool project $$project uses reserved binary name init." >&2; \
			exit 1; \
		fi; \
		cp -a "$$project/bin/." "$(OUT_DIR)/bin/"; \
	done

init: tools
	@set -eu; \
	project="$(firstword $(INIT_PROJECTS))"; \
	echo "[INIT] $$project"; \
	EINC="$(EINC)"; \
	seen=" "; \
	for library in "$(OUT_DIR)"/lib/lib*.a "$(OUT_DIR)"/lib/lib*.so; do \
		[ -f "$$library" ] || continue; \
		name=$${library##*/}; \
		case "$$name" in \
			lib*.a) name=$${name#lib}; name=$${name%.a} ;; \
			lib*.so) name=$${name#lib}; name=$${name%.so} ;; \
			*) continue ;; \
		esac; \
		case "$$seen" in *" $$name "*) continue ;; esac; \
		seen="$$seen$$name "; \
		EINC="$$EINC -l$$name"; \
	done; \
	EINC="$$EINC" $(MAKE) -C "$$project"; \
	test -d "$$project/bin" || { echo "Init project $$project did not produce bin/." >&2; exit 1; }; \
	count=$$(find "$$project/bin" -mindepth 1 -maxdepth 1 -type f | wc -l); \
	if [ "$$count" -ne 1 ] || [ ! -x "$$project/bin/init" ]; then \
		echo "Init project $$project must produce exactly one executable: bin/init." >&2; \
		exit 1; \
	fi; \
	cp -a "$$project/bin/init" "$(OUT_DIR)/bin/init"

stage-rootfs: init
	@mkdir -p "$(ROOTFS_STAGE)/usr/bin" "$(ROOTFS_STAGE)/usr/lib"
	@if [ -d "$(ROOTFS_DIR)" ]; then cp -a "$(ROOTFS_DIR)/." "$(ROOTFS_STAGE)/"; fi
	@mkdir -p "$(ROOTFS_STAGE)/dev" "$(ROOTFS_STAGE)/proc" "$(ROOTFS_STAGE)/sys" \
		"$(ROOTFS_STAGE)/run" "$(ROOTFS_STAGE)/tmp" "$(ROOTFS_STAGE)/root"
	@chmod 1777 "$(ROOTFS_STAGE)/tmp"
	@cp -a "$(OUT_DIR)/bin/." "$(ROOTFS_STAGE)/usr/bin/"
	@cp -a "$(OUT_DIR)/lib/." "$(ROOTFS_STAGE)/usr/lib/"
	@install -D "$(OUT_DIR)/bin/init" "$(ROOTFS_STAGE)/init"
	@mkdir -p "$(ROOTFS_STAGE)/sbin"
	@ln -sf ../init "$(ROOTFS_STAGE)/sbin/init"

$(INITRAMFS_IMAGE):
	@mkdir -p "$(dir $(INITRAMFS_IMAGE))"
	@set -eu; \
	case "$(INITRAMFS_TOOL)" in \
		dracut) \
			command -v "$(DRACUT)" >/dev/null 2>&1 || { echo "Initramfs tool not found: $(DRACUT)." >&2; exit 127; }; \
			"$(DRACUT)" --force --no-hostonly \
				-o "plymouth systemd systemd-initrd systemd-ask-password console-setup" \
				--kver "$(KERNEL_RELEASE)" "$(INITRAMFS_IMAGE)" ;; \
		mkinitcpio) \
			command -v "$(MKINITCPIO)" >/dev/null 2>&1 || { echo "Initramfs tool not found: $(MKINITCPIO)." >&2; exit 127; }; \
			"$(MKINITCPIO)" -k "$(KERNEL_RELEASE)" -g "$(INITRAMFS_IMAGE)" ;; \
		mkinitramfs) \
			command -v "$(MKINITRAMFS)" >/dev/null 2>&1 || { echo "Initramfs tool not found: $(MKINITRAMFS)." >&2; exit 127; }; \
			"$(MKINITRAMFS)" -o "$(INITRAMFS_IMAGE)" "$(KERNEL_RELEASE)" ;; \
		auto) \
			if command -v "$(DRACUT)" >/dev/null 2>&1; then \
				"$(DRACUT)" --force --no-hostonly \
					-o "plymouth systemd systemd-initrd systemd-ask-password console-setup" \
					--kver "$(KERNEL_RELEASE)" "$(INITRAMFS_IMAGE)"; \
			elif command -v "$(MKINITRAMFS)" >/dev/null 2>&1; then \
				"$(MKINITRAMFS)" -o "$(INITRAMFS_IMAGE)" "$(KERNEL_RELEASE)"; \
			elif command -v "$(MKINITCPIO)" >/dev/null 2>&1; then \
				"$(MKINITCPIO)" -k "$(KERNEL_RELEASE)" -g "$(INITRAMFS_IMAGE)"; \
			else \
				echo "Install mkinitramfs, dracut, or mkinitcpio, or set INITRAMFS_TOOL to an installed tool." >&2; \
				exit 127; \
			fi ;; \
		*) \
			echo "Unsupported INITRAMFS_TOOL '$(INITRAMFS_TOOL)' (use auto, mkinitramfs, dracut, or mkinitcpio)." >&2; \
			exit 2 ;; \
	esac

initramfs: $(INITRAMFS_IMAGE)

vda disk.vda: stage-rootfs
	@mkdir -p "$(dir $(VDA_IMAGE))"
	@test -x "$(ROOTFS_STAGE)/init" || { echo "The disk rootfs must contain executable /init." >&2; exit 1; }
	@if [ ! -f "$(VDA_IMAGE)" ]; then \
		truncate -s "$(VDA_SIZE)" "$(VDA_IMAGE)"; \
		mkfs.ext4 -F -d "$(ROOTFS_STAGE)" "$(VDA_IMAGE)"; \
	else \
		echo "[VDA] Updating $(VDA_IMAGE) via e2tools..."; \
		( \
			cd "$(ROOTFS_STAGE)"; \
			find . -mindepth 1 -type d | sed 's|^\./||' | while IFS= read -r dir; do \
				e2mkdir -G 0 -O 0 "$(VDA_IMAGE):/$$dir" 2>/dev/null || true; \
			done; \
			find . -type f | sed 's|^\./||' | while IFS= read -r file; do \
				e2cp -p -G 0 -O 0 "$$file" "$(VDA_IMAGE):/$$file"; \
			done; \
		); \
	fi



qemu-all: stage-rootfs $(INITRAMFS_IMAGE) vda

run: $(INITRAMFS_IMAGE) vda
	@test -r "$(KERNEL_IMAGE)" || { echo "Kernel image not found: $(KERNEL_IMAGE)" >&2; exit 1; }
	$(QEMU) -m "$(QEMU_MEMORY)" \
		-kernel "$(KERNEL_IMAGE)" \
		-initrd "$(INITRAMFS_IMAGE)" \
		-drive "file=$(VDA_IMAGE),format=raw,if=virtio" \
		-append "$(QEMU_APPEND)" \
		-no-reboot \
		-global isa-fdc.fdtypeA=none \
		$(QEMU_EXTRA_ARGS)

clean:
	@set -eu; \
	for project in $(PROJECTS); do \
		if [ -f "$$project/Makefile" ] || [ -f "$$project/makefile" ]; then \
			$(MAKE) -C "$$project" clean; \
		fi; \
	done
	rm -rf "$(OUT_DIR)" "$(ROOTFS_STAGE)" "$(INITRAMFS_IMAGE)"

distclean: clean
	rm -rf "$(BUILD_DIR)" "$(VDA_IMAGE)"
