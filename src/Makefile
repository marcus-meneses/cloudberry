SHELL := /bin/bash
.DEFAULT_GOAL := build
ROOT := $(abspath ..)
BUILD := ../bin
TEST_BUILD := $(ROOT)/bin/tests/isolated
TOOLCHAIN_DIR ?= $(ROOT)/.toolchain
CROSS_PREFIX ?= $(TOOLCHAIN_DIR)/cross/bin/x86_64-elf-
LIMINE_DIR ?= $(TOOLCHAIN_DIR)/limine
CC := $(CROSS_PREFIX)gcc
KERNEL := $(BUILD)/kernel.elf
ISO := $(BUILD)/cloudberry.iso
BOOT_DRIVERS_MIB ?= 512
BOOT_DRIVERS_SOURCE ?= $(BUILD)/src-handler-bundles
# Distinct object/staging names keep archived implementation artifacts out.
STORAGE_WRITE_TEST ?= 0
STORAGE_TEST ?= 0
USB_HUB_TEST ?= 0
USB_HOTPLUG ?= 0
XHCI_HOTPLUG ?= 0
X2APIC ?= 0
ifeq ($(filter $(X2APIC),0 1),)
$(error X2APIC must be 0 or 1)
endif
COMMON_OBJECTS := $(BUILD)/src-boot-spaces.o $(BUILD)/src-spaces.o $(BUILD)/src-boot-memory.o $(BUILD)/src-modules.o $(BUILD)/src-mediate.o $(BUILD)/src-limine-x2apic$(X2APIC).o $(BUILD)/src-cpu.o
OBJECTS := $(BUILD)/src-main.o $(COMMON_OBJECTS)
SPLASH_MODULE := $(BUILD)/kmodules/splash.kmod
MEM_MODULE := $(BUILD)/kmodules/mem.kmod
HWENUM_MODULE := $(BUILD)/kmodules/hwenum.kmod
INTHANDLE_MODULE := $(BUILD)/kmodules/inthandle.kmod
EVENT_MODULE := $(BUILD)/kmodules/event.kmod
IO_MODULE := $(BUILD)/kmodules/io.kmod
HANDLES_MODULE := $(BUILD)/kmodules/handles.kmod
PROCESS_MODULE := $(BUILD)/kmodules/process.kmod
THREADS_MODULE := $(BUILD)/kmodules/threads.kmod
IOMMU_MODULE := $(BUILD)/kmodules/iommu.kmod
PCI_MODULE := $(BUILD)/kmodules/pci.kmod
SATA_MODULE := $(BUILD)/kmodules/sata.kmod
NVME_MODULE := $(BUILD)/kmodules/nvme.kmod
USB_STORAGE_MODULE := $(BUILD)/kmodules/usb-storage.kmod
PARTITION_MODULE := $(BUILD)/kmodules/partition.kmod
EXT4_FS_MODULE := $(BUILD)/kmodules/ext4-fs.kmod
BOOTSTRAP_MODULE := $(BUILD)/kmodules/bootstrap.kmod
CONFIG_MODULE := $(BUILD)/kmodules/config.kmod
USB_HUB_MODULE := $(BUILD)/kmodules/usb-hub.kmod
USB_MODULE := $(BUILD)/kmodules/usb.kmod
ACPI_MODULE := $(BUILD)/kmodules/acpi.kmod
PLATFORM_MODULE := $(BUILD)/kmodules/platform.kmod
XHCI_MODULE := $(BUILD)/kmodules/xhci.kmod
IRQ_MODULE := $(BUILD)/kmodules/irq.kmod
SMP_MODULE := $(BUILD)/kmodules/smp.kmod
APIC_MODULE := $(BUILD)/kmodules/apic.kmod
TIMER_MODULE := $(BUILD)/kmodules/timer.kmod
CLOCK_MODULE := $(BUILD)/kmodules/clock.kmod
MMIO_MODULE := $(BUILD)/kmodules/mmio.kmod
SCHEDULER_MODULE := $(BUILD)/kmodules/scheduler.kmod
OBJCOPY := $(CROSS_PREFIX)objcopy
STAGE := $(BUILD)/src-iso-root
NVME_DISK ?= $(BUILD)/cloudberry-nvme.img
SATA_DISK ?= $(BUILD)/cloudberry-sata.img
OVMF ?= /usr/share/ovmf/OVMF.fd
QEMU ?= qemu-system-x86_64
QEMU_ACCEL ?= tcg
# VM topology: one socket, QEMU_CPUS cores, one thread per core.
QEMU_CPUS ?= 1
QEMU_DISPLAY ?= gtk
QEMU_IOMMU ?= none
ifeq ($(filter $(QEMU_IOMMU),none intel amd),)
$(error QEMU_IOMMU must be none, intel or amd)
endif
comma := ,
QEMU_IOMMU_FLAGS = $(if $(filter intel,$(QEMU_IOMMU)),-device intel-iommu$(comma)intremap=off,$(if $(filter amd,$(QEMU_IOMMU)),-device amd-iommu$(comma)intremap=off,))
QEMU_CPU ?= $(if $(filter kvm,$(QEMU_ACCEL)),host$(comma)migratable=off$(comma)+invtsc,max)
CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding \
    -fno-stack-protector -fno-pic -fno-pie -m64 -mno-red-zone \
    -mgeneral-regs-only -mcmodel=kernel -MMD -MP
LDFLAGS := -nostdlib -static -no-pie -Wl,--build-id=none \
    -Wl,-z,noexecstack -Wl,-z,max-page-size=0x1000 -Wl,-T,boot/linker.ld

# USB boot medium avoids attaching a legacy IDE optical drive. SATA uses AHCI.
QEMU_FLAGS = -no-user-config -nodefaults -machine q35 -m 512M \
    $(QEMU_IOMMU_FLAGS) -accel $(QEMU_ACCEL) -cpu $(QEMU_CPU) -smp cpus=$(QEMU_CPUS),sockets=1,cores=$(QEMU_CPUS),threads=1 -bios "$(OVMF)" \
    -device VGA -device qemu-xhci,id=xhci \
    -device usb-kbd,bus=xhci.0 -device usb-mouse,bus=xhci.0 \
    -drive if=none,id=bootmedia,format=raw,readonly=on,file="$(ISO)" \
    -device usb-bot,id=bootusb,bus=xhci.0 \
    -device scsi-hd,bus=bootusb.0,drive=bootmedia,bootindex=1 \
    -drive if=none,id=nvmedisk,format=raw,file="$(NVME_DISK)" \
    -device nvme,drive=nvmedisk,serial=CLOUDBERRY2NVME \
    -drive if=none,id=satadisk,format=raw,file="$(SATA_DISK)" \
    -device ide-hd,bus=ide.0,drive=satadisk \
    -display $(QEMU_DISPLAY) -name Cloudberry-v2 \
    -debugcon stdio -global isa-debugcon.iobase=0xe9 \
    -monitor none -no-reboot -no-shutdown

.PHONY: build run debug clean check-tools FORCE
build: $(ISO) $(NVME_DISK) $(SATA_DISK) $(BUILD)/kmodules/mem.o
check-tools:
	@test -x "$(CC)" || { echo 'Missing cross compiler in .toolchain'; exit 1; }
	@test -f "$(LIMINE_DIR)/BOOTX64.EFI" -a -f "$(LIMINE_DIR)/limine-uefi-cd.bin"
	@command -v xorriso >/dev/null
	@command -v mke2fs >/dev/null
	@command -v sgdisk >/dev/null

$(BUILD)/src-main.o: kernel/main.c kernel/modules.h kernel/surfaces.h boot/memory.h | check-tools
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# Always relink/repackage candidate output from the active source tree.
$(BUILD)/src-logo.rgb: assets/cloudberry.png tools/pack-logo.py
	@mkdir -p $(BUILD)
	python3 tools/pack-logo.py $< $@

$(BUILD)/src-limine-x2apic$(X2APIC).o: boot/limine.c boot/limine.h boot/memory.h | check-tools
	$(CC) $(CFLAGS) -DBOOT_PREFER_X2APIC=$(X2APIC) -c $< -o $@

$(KERNEL): $(OBJECTS) boot/linker.ld FORCE
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

$(ISO): $(BUILD)/src-handler-bundles/.ready $(IO_MODULE) $(HANDLES_MODULE) $(PROCESS_MODULE) $(KERNEL) $(MEM_MODULE) $(HWENUM_MODULE) $(INTHANDLE_MODULE) $(SCHEDULER_MODULE) $(MMIO_MODULE) $(CLOCK_MODULE) $(TIMER_MODULE) $(EVENT_MODULE) $(SPLASH_MODULE) $(APIC_MODULE) $(THREADS_MODULE) $(SMP_MODULE) $(IRQ_MODULE) $(PCI_MODULE) $(NVME_MODULE) $(SATA_MODULE) $(IOMMU_MODULE) $(XHCI_MODULE) $(ACPI_MODULE) $(PLATFORM_MODULE) $(USB_MODULE) $(USB_HUB_MODULE) $(USB_STORAGE_MODULE) $(PARTITION_MODULE) $(EXT4_FS_MODULE) $(BOOTSTRAP_MODULE) $(CONFIG_MODULE) boot/cloudberry.cnf boot/limine.conf tools/build-boot-image.py
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/boot/limine $(STAGE)/boot/kmodules $(STAGE)/EFI/BOOT
	cp $(KERNEL) $(STAGE)/boot/kernel.elf
	cp $(BUILD)/kmodules/xstate.kmod $(BACKENDS) $(STAGE)/boot/kmodules/
	cp $(MEM_MODULE) $(STAGE)/boot/kmodules/mem.kmod
	cp $(HWENUM_MODULE) $(STAGE)/boot/kmodules/hwenum.kmod
	cp $(INTHANDLE_MODULE) $(STAGE)/boot/kmodules/inthandle.kmod
	cp $(SCHEDULER_MODULE) $(STAGE)/boot/kmodules/scheduler.kmod
	cp $(MMIO_MODULE) $(STAGE)/boot/kmodules/mmio.kmod
	cp $(CLOCK_MODULE) $(STAGE)/boot/kmodules/clock.kmod
	cp $(TIMER_MODULE) $(STAGE)/boot/kmodules/timer.kmod
	cp $(APIC_MODULE) $(STAGE)/boot/kmodules/apic.kmod
	cp $(IO_MODULE) $(HANDLES_MODULE) $(PROCESS_MODULE) $(THREADS_MODULE) $(SMP_MODULE) $(IRQ_MODULE) $(PCI_MODULE) $(NVME_MODULE) $(SATA_MODULE) $(IOMMU_MODULE) $(XHCI_MODULE) $(ACPI_MODULE) $(PLATFORM_MODULE) $(USB_MODULE) $(USB_HUB_MODULE) $(USB_STORAGE_MODULE) $(PARTITION_MODULE) $(EXT4_FS_MODULE) $(BOOTSTRAP_MODULE) $(CONFIG_MODULE) $(STAGE)/boot/kmodules/
	cp $(EVENT_MODULE) $(STAGE)/boot/kmodules/event.kmod
	cp $(SPLASH_MODULE) $(STAGE)/boot/kmodules/splash.kmod
	cp boot/cloudberry.cnf $(STAGE)/boot/cloudberry.cnf
	cp boot/limine.conf $(STAGE)/boot/limine/limine.conf
	cp $(LIMINE_DIR)/limine-uefi-cd.bin $(STAGE)/boot/limine/
	cp $(LIMINE_DIR)/BOOTX64.EFI $(STAGE)/EFI/BOOT/
	python3 tools/build-boot-image.py "$(STAGE)" "$@" --drivers-mib=$(BOOT_DRIVERS_MIB) $(if $(strip $(BOOT_DRIVERS_SOURCE)),--drivers-source="$(BOOT_DRIVERS_SOURCE)",)

# Sparse, blank 1 GiB media, created once; no filesystem or boot-time formatting.
$(NVME_DISK) $(SATA_DISK):
	@mkdir -p $(@D)
	qemu-img create -f raw "$@" 1G

run: build
	@test -f "$(OVMF)" || { echo 'Set OVMF to the installed UEFI firmware path'; exit 1; }
	bash boot/run-qemu.sh $(QEMU) $(QEMU_FLAGS)

debug: build
	bash boot/run-qemu.sh $(QEMU) $(QEMU_FLAGS) -S -gdb tcp:127.0.0.1:1234

# Persistent virtual disks are retained by clean.
clean:
	rm -f $(BUILD)/src-*.o $(BUILD)/src-*.d $(BUILD)/src-logo.rgb $(KERNEL) $(ISO)
	rm -rf $(BUILD)/kmodules $(STAGE) $(ISO).parts

# Native debug object for the standalone allocator; the loaded module uses PIC below.
$(BUILD)/kmodules/mem.o: kmodules/shared/mem.c kmodules/shared/mem.h | check-tools
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -fno-strict-aliasing -c $< -o $@

.PHONY: test-mem
test-mem:
	@mkdir -p $(BUILD)/tests
	gcc -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fno-strict-aliasing \
	    -fsanitize=undefined tests/mem.c kmodules/shared/mem.c -o $(BUILD)/tests/src-mem
	$(BUILD)/tests/src-mem

$(BUILD)/src-boot-memory.o: boot/memory.c boot/memory.h | check-tools
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/src-modules.o: kernel/modules.c kernel/modules.h kernel/surfaces.h boot/memory.h kmodules/shared/module.h kmodules/shared/hwenum.h | check-tools
	$(CC) $(CFLAGS) -c $< -o $@

# PIC kmodule, loaded separately by Limine; no mem implementation in kernel.elf.
$(BUILD)/tests/src-main-test.o: kernel/main.c kernel/modules.h kernel/surfaces.h boot/memory.h | check-tools
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -DBOOT_MEM_TEST -c $< -o $@

$(BUILD)/tests/src-mem-boot.o: tests/mem-boot.c kernel/modules.h kernel/surfaces.h kmodules/shared/mem.h kmodules/shared/hwenum.h kmodules/shared/inthandle.h boot/memory.h | check-tools
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -DBOOT_MEM_TEST -c $< -o $@

$(BUILD)/tests/src-kernel.elf: FORCE $(BUILD)/tests/src-main-test.o $(BUILD)/tests/src-mem-boot.o $(BUILD)/tests/src-acpi-boot.o $(BUILD)/tests/src-usb-boot$(USB_HOTPLUG)-hub$(USB_HUB_TEST)-storage$(STORAGE_TEST)-write$(STORAGE_WRITE_TEST).o $(BUILD)/tests/src-scheduler-boot.o $(BUILD)/tests/src-mmio-boot.o $(BUILD)/tests/src-clock-boot.o $(BUILD)/tests/src-events-boot.o $(BUILD)/tests/src-mem-pages-boot.o $(BUILD)/tests/src-stack-call.o $(BUILD)/tests/src-threads-boot.o $(BUILD)/tests/src-irq-boot.o $(BUILD)/tests/src-nvme-boot.o $(BUILD)/tests/src-xhci-boot$(XHCI_HOTPLUG).o $(COMMON_OBJECTS) boot/linker.ld
	$(CC) $(LDFLAGS) $(filter %.o,$^) -o $@

.PHONY: test-mem-boot
test-mem-boot:
	$(MAKE) --no-print-directory BUILD="$(TEST_BUILD)" ISO="$(TEST_BUILD)/cloudberry.iso" KERNEL="$(TEST_BUILD)/kernel.elf" STAGE="$(TEST_BUILD)/src-iso-root" NVME_DISK="$(TEST_BUILD)/nvme.img" SATA_DISK="$(TEST_BUILD)/sata.img" _test-mem-boot

.PHONY: _test-mem-boot
_test-mem-boot: $(ISO) $(BUILD)/tests/src-kernel.elf $(BUILD)/kmodules/probe.kmod
	python3 tests/mem-boot.py "$(abspath $(BUILD))"

.PHONY: test-hwenum test-hwenum-boot
test-hwenum:
	@mkdir -p $(BUILD)/tests
	gcc -std=gnu11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fno-strict-aliasing \
	    -fsanitize=undefined tests/hwenum.c kmodules/shared/hwenum.c kmodules/shared/mem.c -o $(BUILD)/tests/src-hwenum
	$(BUILD)/tests/src-hwenum

test-hwenum-boot: test-mem-boot

MODULE_CFLAGS = $(filter-out -fno-pic -fno-pie -mcmodel=kernel,$(CFLAGS)) -fPIE -fvisibility=hidden -mcmodel=small -fno-strict-aliasing
MODULE_HEADERS = kmodules/shared/module.h kernel/surfaces.h boot/memory.h kernel/cpu.h kernel/cpu-profile.h

$(BUILD)/src-mediate.o: kernel/mediate.S
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/kmodules/%-pic.o: kmodules/shared/%.c $(MODULE_HEADERS) Makefile
	@mkdir -p $(@D)
	$(CC) $(MODULE_CFLAGS) -c $< -o $@

$(BUILD)/kmodules/%.exports.S: kmodules/shared/%.json tools/module-exports.py
	@mkdir -p $(@D)
	python3 tools/module-exports.py $< $@

$(BUILD)/kmodules/%.exports.o: $(BUILD)/kmodules/%.exports.S
	$(CC) $(MODULE_CFLAGS) -c $< -o $@

$(BUILD)/kmodules/inthandle-entry.o: kmodules/shared/inthandle-entry.S
	@mkdir -p $(@D)
	$(CC) $(MODULE_CFLAGS) -c $< -o $@

$(BUILD)/kmodules/mem.elf: $(BUILD)/kmodules/mem-pic.o $(BUILD)/kmodules/mem-module-pic.o $(BUILD)/kmodules/mem-pages-pic.o $(BUILD)/kmodules/mem.exports.o kmodules/shared/module.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@

$(BUILD)/kmodules/hwenum.elf: $(BUILD)/kmodules/hwenum-pic.o $(BUILD)/kmodules/hwenum-module-pic.o $(BUILD)/kmodules/hwenum.exports.o kmodules/shared/module.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@

$(BUILD)/kmodules/inthandle.elf: $(BUILD)/kmodules/inthandle-pic.o $(BUILD)/kmodules/inthandle-entry.o $(BUILD)/kmodules/inthandle.exports.o kmodules/shared/module.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@

$(BUILD)/kmodules/%.kmod: $(BUILD)/kmodules/%.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/kmodules/mem-pic.o $(BUILD)/kmodules/mem-module-pic.o: kmodules/shared/mem.h
$(BUILD)/kmodules/hwenum-pic.o $(BUILD)/kmodules/hwenum-module-pic.o: kmodules/shared/hwenum.h
$(BUILD)/kmodules/inthandle-pic.o: kmodules/shared/inthandle.h

$(BUILD)/kmodules/probe-pic.o: tests/probe.c tests/probe.h kmodules/shared/inthandle.h $(MODULE_HEADERS) Makefile
	@mkdir -p $(@D)
	$(CC) $(MODULE_CFLAGS) -c $< -o $@

$(BUILD)/kmodules/probe.exports.S: tests/probe.json tools/module-exports.py
	@mkdir -p $(@D)
	python3 tools/module-exports.py $< $@

$(BUILD)/kmodules/probe.elf: $(BUILD)/kmodules/probe-process-pic.o $(BUILD)/kmodules/space-fault-pic.o $(BUILD)/kmodules/space-fault-entry.o $(BUILD)/kmodules/probe-spaces-pic.o $(BUILD)/kmodules/probe-pic.o $(BUILD)/kmodules/probe-xhci-pic.o $(BUILD)/kmodules/probe-usb-pic.o $(BUILD)/kmodules/probe-hubs-pic.o $(BUILD)/kmodules/probe-storage-pic.o $(BUILD)/kmodules/probe-block-pic.o $(BUILD)/kmodules/probe.exports.o kmodules/shared/module.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@

.PHONY: test-reservations
test-reservations:
	@mkdir -p $(BUILD)/tests
	gcc -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fsanitize=undefined \
	    tests/reservations.c boot/memory.c -o $(BUILD)/tests/src-reservations
	$(BUILD)/tests/src-reservations

$(BUILD)/tests/src-fault-%.o: kernel/main.c kernel/modules.h kernel/surfaces.h boot/memory.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -DBOOT_FAULT=$* -c $< -o $@

$(BUILD)/tests/src-fault-%.elf: $(BUILD)/tests/src-fault-%.o $(COMMON_OBJECTS) boot/linker.ld
	$(CC) $(LDFLAGS) $(filter %.o,$^) -o $@

.PHONY: test-inthandle-faults
test-inthandle-faults:
	$(MAKE) --no-print-directory BUILD="$(TEST_BUILD)" ISO="$(TEST_BUILD)/cloudberry.iso" KERNEL="$(TEST_BUILD)/kernel.elf" STAGE="$(TEST_BUILD)/src-iso-root" NVME_DISK="$(TEST_BUILD)/nvme.img" SATA_DISK="$(TEST_BUILD)/sata.img" _test-inthandle-faults

.PHONY: _test-inthandle-faults
_test-inthandle-faults: $(ISO) $(addprefix $(BUILD)/tests/src-fault-,1.elf 2.elf 3.elf)
	python3 tests/inthandle-faults.py "$(abspath $(BUILD))"

# Dependencies are compiler side effects, never targets for implicit linking.
$(BUILD)/%.d: ;
.SECONDARY:

-include $(wildcard $(BUILD)/src-*.d $(BUILD)/kmodules/*.d $(BUILD)/tests/src-*.d)

$(BUILD)/kmodules/scheduler-pic.o: kmodules/shared/scheduler.h kmodules/shared/inthandle.h
$(BUILD)/kmodules/scheduler.elf: $(BUILD)/kmodules/scheduler-pic.o $(BUILD)/kmodules/scheduler.exports.o kmodules/shared/module.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@

.PHONY: test-scheduler test-scheduler-boot
test-scheduler:
	@mkdir -p $(BUILD)/tests
	gcc -std=gnu11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fno-strict-aliasing \
	    -fsanitize=undefined tests/scheduler.c kmodules/shared/scheduler.c kmodules/shared/mem.c -o $(BUILD)/tests/src-scheduler
	$(BUILD)/tests/src-scheduler

test-scheduler-boot: test-mem-boot

$(BUILD)/tests/src-scheduler-boot.o: tests/scheduler-boot.c tests/probe.h kmodules/shared/scheduler.h kernel/surfaces.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/kmodules/mmio.elf: $(BUILD)/kmodules/mmio-pic.o $(BUILD)/kmodules/mmio.exports.o kmodules/shared/module.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@

.PHONY: test-mmio test-mmio-boot
test-mmio:
	@mkdir -p $(BUILD)/tests
	gcc -std=gnu11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fno-strict-aliasing \
	    -fsanitize=undefined tests/mmio.c kmodules/shared/mmio.c kmodules/shared/mem.c -o $(BUILD)/tests/src-mmio
	$(BUILD)/tests/src-mmio

test-mmio-boot: test-mem-boot

$(BUILD)/tests/src-mmio-boot.o: tests/mmio-boot.c kmodules/shared/mmio.h kmodules/shared/hwenum.h kernel/surfaces.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/kmodules/clock.elf: $(BUILD)/kmodules/clock-pic.o $(BUILD)/kmodules/clock-x86-pic.o $(BUILD)/kmodules/clock.exports.o kmodules/shared/module.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@

.PHONY: test-clock test-clock-boot
test-clock:
	@mkdir -p $(BUILD)/tests
	gcc -std=gnu11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fsanitize=undefined \
	    tests/clock.c kmodules/shared/clock.c -o $(BUILD)/tests/src-clock
	@for mode in tsc hpet enumerated stalled-enumerated no-rtc no-century shared-tsc shared-tsc-drift shared-tsc-profile-fault shared-tsc-pause-before shared-tsc-pause-after shared-tsc-lag shared-tsc-bracket-regression; do $(BUILD)/tests/src-clock $$mode || exit $$?; done

test-clock-boot: test-mem-boot

$(BUILD)/tests/src-clock-boot.o: tests/clock-boot.c kmodules/shared/clock.h kernel/surfaces.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/kmodules/timer.elf: $(BUILD)/kmodules/timer-pic.o $(BUILD)/kmodules/timer-x86-pic.o $(BUILD)/kmodules/timer.exports.o kmodules/shared/module.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@

.PHONY: test-timer test-events-boot
test-timer:
	@mkdir -p $(BUILD)/tests
	gcc -std=gnu11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fsanitize=undefined \
	    tests/timer.c kmodules/shared/timer.c -o $(BUILD)/tests/src-timer
	$(BUILD)/tests/src-timer

test-events-boot: test-mem-boot

$(BUILD)/tests/src-events-boot.o: tests/events-boot.c kmodules/shared/event.h kmodules/shared/timer.h kmodules/shared/scheduler.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/kmodules/event.elf: $(BUILD)/kmodules/event-pic.o $(BUILD)/kmodules/event.exports.o kmodules/shared/module.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@

$(BUILD)/kmodules/splash-logo.o: kmodules/shared/splash-logo.S $(BUILD)/src-logo.rgb
	$(CC) $(MODULE_CFLAGS) -DSPLASH_LOGO_PATH='"$(abspath $(BUILD)/src-logo.rgb)"' -c $< -o $@

$(BUILD)/kmodules/splash.elf: $(BUILD)/kmodules/splash-pic.o $(BUILD)/kmodules/splash-module-pic.o $(BUILD)/kmodules/splash-logo.o $(BUILD)/kmodules/splash.exports.o kmodules/shared/module.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@

.PHONY: test-splash
test-splash:
	@mkdir -p $(BUILD)/tests
	gcc -std=gnu11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fsanitize=undefined \
	    tests/splash.c kmodules/shared/splash.c -o $(BUILD)/tests/src-splash
	$(BUILD)/tests/src-splash

$(BUILD)/kmodules/apic.elf: $(BUILD)/kmodules/apic-pic.o $(BUILD)/kmodules/apic-x86-pic.o $(BUILD)/kmodules/apic.exports.o kmodules/shared/module.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@

.PHONY: test-apic test-pages
test-apic:
	@mkdir -p $(BUILD)/tests
	gcc -std=gnu11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fsanitize=undefined tests/apic.c kmodules/shared/apic.c -o $(BUILD)/tests/src-apic
	$(BUILD)/tests/src-apic
	$(BUILD)/tests/src-apic x2apic

test-pages:
	@mkdir -p $(BUILD)/tests
	gcc -std=gnu11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fno-strict-aliasing -fsanitize=undefined tests/mem-pages.c kmodules/shared/mem-pages.c kmodules/shared/mem.c boot/memory.c -o $(BUILD)/tests/src-mem-pages
	$(BUILD)/tests/src-mem-pages

$(BUILD)/tests/src-mem-pages-boot.o: tests/mem-pages-boot.c kmodules/shared/mem-pages.h boot/memory.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: test-startup-failure _test-startup-failure
test-startup-failure:
	$(MAKE) --no-print-directory BUILD="$(TEST_BUILD)" ISO="$(TEST_BUILD)/cloudberry.iso" KERNEL="$(TEST_BUILD)/kernel.elf" STAGE="$(TEST_BUILD)/src-iso-root" NVME_DISK="$(TEST_BUILD)/nvme.img" SATA_DISK="$(TEST_BUILD)/sata.img" _test-startup-failure

_test-startup-failure: $(ISO) $(BUILD)/tests/src-kernel.elf $(BUILD)/kmodules/probe.kmod
	python3 tests/mem-boot.py "$(abspath $(BUILD))" --missing-clock

$(BUILD)/tests/src-stack-call.o: tests/stack-call.S
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/src-cpu.o: kernel/cpu.c kernel/cpu.h kernel/cpu-profile.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/kmodules/threads-entry.o: kmodules/shared/threads-entry.S
	@mkdir -p $(@D)
	$(CC) $(MODULE_CFLAGS) -c $< -o $@
$(BUILD)/kmodules/threads.elf: $(BUILD)/kmodules/threads-pic.o $(BUILD)/kmodules/threads-entry.o $(BUILD)/kmodules/threads.exports.o kmodules/shared/module.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@

$(BUILD)/kmodules/smp-entry.o: kmodules/shared/smp-entry.S
	@mkdir -p $(@D)
	$(CC) $(MODULE_CFLAGS) -c $< -o $@
$(BUILD)/kmodules/smp.elf: $(BUILD)/kmodules/smp-pic.o $(BUILD)/kmodules/smp-entry.o $(BUILD)/kmodules/tsc-validation-pic.o $(BUILD)/kmodules/smp.exports.o kmodules/shared/module.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@

$(BUILD)/tests/src-threads-boot.o: tests/threads-boot.c tests/probe.h kmodules/shared/threads.h kmodules/shared/smp.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@
.PHONY: test-threads-state
test-threads-state:
	@mkdir -p $(BUILD)/tests
	gcc -std=gnu11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fsanitize=undefined tests/threads-state.c -o $(BUILD)/tests/src-threads-state
	$(BUILD)/tests/src-threads-state

.PHONY: test-threads-smp _test-threads-smp
test-threads-smp:
	$(MAKE) --no-print-directory BUILD="$(TEST_BUILD)" ISO="$(TEST_BUILD)/cloudberry.iso" KERNEL="$(TEST_BUILD)/kernel.elf" STAGE="$(TEST_BUILD)/src-iso-root" NVME_DISK="$(TEST_BUILD)/nvme.img" SATA_DISK="$(TEST_BUILD)/sata.img" _test-threads-smp
_test-threads-smp: $(ISO) $(BUILD)/tests/src-kernel.elf $(BUILD)/kmodules/probe.kmod
	python3 tests/mem-boot.py "$(abspath $(BUILD))" --cpus=1
	python3 tests/mem-boot.py "$(abspath $(BUILD))" --cpus=4

$(BUILD)/kmodules/irq.elf: $(BUILD)/kmodules/irq-pic.o $(BUILD)/kmodules/irq.exports.o kmodules/shared/module.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@

$(BUILD)/tests/src-irq-boot.o: tests/irq-boot.c tests/probe.h kmodules/shared/irq.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: test-smp-clock test-tsc-profile test-tsc-validation
test-smp-clock:
	@mkdir -p $(BUILD)/tests
	gcc -std=gnu11 -O2 -g -Wall -Wextra -Werror -fsanitize=undefined tests/smp-clock.c kmodules/shared/tsc-validation.c -o $(BUILD)/tests/src-smp-clock
	$(BUILD)/tests/src-smp-clock

test-tsc-profile:
	python3 tests/tsc_profile.py

test-tsc-validation:
	@mkdir -p $(BUILD)/tests
	gcc -std=c11 -O2 -g -Wall -Wextra -Werror -fsanitize=undefined tests/tsc-validation.c kmodules/shared/tsc-validation.c -o $(BUILD)/tests/src-tsc-validation
	$(BUILD)/tests/src-tsc-validation

.PHONY: test-irq
test-irq:
	@mkdir -p $(BUILD)/tests
	gcc -std=gnu11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fsanitize=undefined tests/irq.c kmodules/shared/irq.c -o $(BUILD)/tests/src-irq
	$(BUILD)/tests/src-irq

$(BUILD)/kmodules/pci.elf: $(BUILD)/kmodules/pci-pic.o $(BUILD)/kmodules/pci-x86-pic.o $(BUILD)/kmodules/pci.exports.o kmodules/shared/module.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@

$(BUILD)/kmodules/nvme.elf: $(BUILD)/kmodules/nvme-pic.o $(BUILD)/kmodules/nvme.exports.o kmodules/shared/module.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@

$(BUILD)/tests/src-nvme-boot.o: tests/nvme-boot.c tests/probe.h kmodules/shared/nvme.h kmodules/shared/pci.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: test-pci
test-pci:
	@mkdir -p $(BUILD)/tests
	gcc -std=gnu11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fsanitize=undefined tests/pci.c kmodules/shared/pci.c -o $(BUILD)/tests/src-pci
	$(BUILD)/tests/src-pci
	$(BUILD)/tests/src-pci iommu
	$(BUILD)/tests/src-pci translated

.PHONY: test-nvme-boot
test-nvme-boot: test-mem-boot

$(BUILD)/kmodules/iommu.elf: $(BUILD)/kmodules/iommu-pic.o $(BUILD)/kmodules/iommu.exports.o kmodules/shared/module.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@

.PHONY: test-iommu test-native _test-iommu _test-native
test-iommu:
	$(MAKE) --no-print-directory BUILD="$(TEST_BUILD)" ISO="$(TEST_BUILD)/cloudberry.iso" KERNEL="$(TEST_BUILD)/kernel.elf" STAGE="$(TEST_BUILD)/src-iso-root" NVME_DISK="$(TEST_BUILD)/nvme.img" SATA_DISK="$(TEST_BUILD)/sata.img" _test-iommu
_test-iommu: $(ISO) $(BUILD)/tests/src-kernel.elf $(BUILD)/kmodules/probe.kmod
	python3 tests/mem-boot.py "$(abspath $(BUILD))" --cpus=$(QEMU_CPUS) --iommu=intel
	python3 tests/mem-boot.py "$(abspath $(BUILD))" --cpus=$(QEMU_CPUS) --iommu=amd

test-native:
	python3 tests/native-check.py
	$(MAKE) --no-print-directory BUILD="$(TEST_BUILD)" ISO="$(TEST_BUILD)/cloudberry.iso" KERNEL="$(TEST_BUILD)/kernel.elf" STAGE="$(TEST_BUILD)/src-iso-root" NVME_DISK="$(TEST_BUILD)/nvme.img" SATA_DISK="$(TEST_BUILD)/sata.img" _test-native
_test-native: $(ISO) $(BUILD)/tests/src-kernel.elf $(BUILD)/kmodules/probe.kmod
	python3 tests/mem-boot.py "$(abspath $(BUILD))" --cpus=$(QEMU_CPUS) --iommu=$(QEMU_IOMMU) --accel=kvm --cpu=$(if $(filter command line environment override,$(origin QEMU_CPU)),$(QEMU_CPU),host$(comma)migratable=off$(comma)+invtsc) --x2apic-requested=$(X2APIC)

.PHONY: test-iommu-host
test-iommu-host:
	@mkdir -p $(BUILD)/tests
	gcc -std=gnu11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fsanitize=undefined tests/iommu.c -o $(BUILD)/tests/src-iommu
	$(BUILD)/tests/src-iommu

$(BUILD)/tests/src-lock-watchdog.o: tests/lock-watchdog.c tests/probe.h kernel/cpu.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/tests/src-lock-watchdog.elf: $(BUILD)/tests/src-main-test.o $(BUILD)/tests/src-lock-watchdog.o $(COMMON_OBJECTS) boot/linker.ld
	$(CC) $(LDFLAGS) $(filter %.o,$^) -o $@
.PHONY: test-lock-watchdog _test-lock-watchdog
test-lock-watchdog:
	$(MAKE) --no-print-directory BUILD="$(TEST_BUILD)" ISO="$(TEST_BUILD)/cloudberry.iso" KERNEL="$(TEST_BUILD)/kernel.elf" STAGE="$(TEST_BUILD)/src-iso-root" NVME_DISK="$(TEST_BUILD)/nvme.img" SATA_DISK="$(TEST_BUILD)/sata.img" _test-lock-watchdog
_test-lock-watchdog: $(ISO) $(BUILD)/tests/src-lock-watchdog.elf $(BUILD)/kmodules/probe.kmod
	python3 tests/mem-boot.py "$(abspath $(BUILD))" --cpus=2 --watchdog

# Boot alternatives share frontend signatures, but have independent code/state.
$(BUILD)/kmodules/cpu-ops-pic.o $(BUILD)/kmodules/cpu-tsc-pic.o $(BUILD)/kmodules/cpu-topology-pic.o: kmodules/shared/cpu-platform.h
CPU_BACKENDS = $(BUILD)/kmodules/cpu-intel.kmod $(BUILD)/kmodules/cpu-amd.kmod
IOMMU_BACKENDS = $(BUILD)/kmodules/iommu-intel.kmod $(BUILD)/kmodules/iommu-amd.kmod
BACKENDS = $(CPU_BACKENDS) $(IOMMU_BACKENDS)
$(BUILD)/kmodules/cpu-intel-pic.o: kmodules/intel/cpu-intel.c kmodules/shared/cpu-platform.h $(MODULE_HEADERS) Makefile
	@mkdir -p $(@D)
	$(CC) $(MODULE_CFLAGS) -c $< -o $@
$(BUILD)/kmodules/iommu-intel-pic.o: kmodules/intel/iommu-intel.c kmodules/shared/iommu-internal.h $(MODULE_HEADERS) Makefile
	@mkdir -p $(@D)
	$(CC) $(MODULE_CFLAGS) -c $< -o $@
$(BUILD)/kmodules/cpu-intel.exports.S: kmodules/intel/cpu-intel.json tools/module-exports.py
	@mkdir -p $(@D)
	python3 tools/module-exports.py $< $@
$(BUILD)/kmodules/iommu-intel.exports.S: kmodules/intel/iommu-intel.json tools/module-exports.py
	@mkdir -p $(@D)
	python3 tools/module-exports.py $< $@
$(BUILD)/kmodules/cpu-amd-pic.o: kmodules/amd/cpu-amd.c kmodules/shared/cpu-platform.h $(MODULE_HEADERS) Makefile
	@mkdir -p $(@D)
	$(CC) $(MODULE_CFLAGS) -c $< -o $@
$(BUILD)/kmodules/iommu-amd-pic.o: kmodules/amd/iommu-amd.c kmodules/shared/iommu-internal.h $(MODULE_HEADERS) Makefile
	@mkdir -p $(@D)
	$(CC) $(MODULE_CFLAGS) -c $< -o $@
$(BUILD)/kmodules/cpu-amd.exports.S: kmodules/amd/cpu-amd.json tools/module-exports.py
	@mkdir -p $(@D)
	python3 tools/module-exports.py $< $@
$(BUILD)/kmodules/iommu-amd.exports.S: kmodules/amd/iommu-amd.json tools/module-exports.py
	@mkdir -p $(@D)
	python3 tools/module-exports.py $< $@
$(BUILD)/kmodules/cpu-%.elf: $(BUILD)/kmodules/cpu-%-pic.o $(BUILD)/kmodules/cpu-ops-pic.o $(BUILD)/kmodules/cpu-topology-pic.o $(BUILD)/kmodules/cpu-tsc-pic.o $(BUILD)/kmodules/cpu-%.exports.o kmodules/shared/module.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@
$(BUILD)/kmodules/iommu-%.elf: $(BUILD)/kmodules/iommu-%-pic.o $(BUILD)/kmodules/iommu-%.exports.o kmodules/shared/module.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@
$(ISO): $(BACKENDS) $(BUILD)/kmodules/xstate.kmod

$(BUILD)/kmodules/xstate.elf: $(BUILD)/kmodules/xstate-pic.o $(BUILD)/kmodules/xstate.exports.o kmodules/shared/module.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@

.PHONY: test-portability test-cpu-topology test-cpu-tsc test-cpu-ordering test-cpu-profile _test-portability
.PHONY: test-interfaces
test-interfaces:
	@mkdir -p $(BUILD)/tests
	gcc -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=undefined tests/interfaces.c -o $(BUILD)/tests/src-interfaces-c
	$(BUILD)/tests/src-interfaces-c
	g++ -std=c++20 -O2 -Wall -Wextra -Werror -fsanitize=undefined -x c++ tests/interfaces.c -o $(BUILD)/tests/src-interfaces-cpp
	$(BUILD)/tests/src-interfaces-cpp
test-cpu-profile:
	@mkdir -p $(BUILD)/tests
	gcc -std=c11 -O2 -g -Wall -Wextra -Werror -fsanitize=undefined tests/cpu-profile.c -o $(BUILD)/tests/src-cpu-profile
	$(BUILD)/tests/src-cpu-profile
test-cpu-ordering:
	@mkdir -p $(BUILD)/tests
	gcc -std=c11 -O2 -g -Wall -Wextra -Werror -fsanitize=undefined -DTEST_CPU_ORDERING tests/cpu-tsc.c kmodules/shared/cpu-tsc.c kmodules/shared/cpu-topology.c kmodules/amd/cpu-amd.c -o $(BUILD)/tests/src-cpu-ordering
	$(BUILD)/tests/src-cpu-ordering l
	$(BUILD)/tests/src-cpu-ordering r
	$(BUILD)/tests/src-cpu-ordering c
test-cpu-tsc:
	@mkdir -p $(BUILD)/tests
	gcc -std=c11 -O2 -g -Wall -Wextra -Werror -fsanitize=undefined tests/cpu-tsc.c kmodules/shared/cpu-tsc.c -o $(BUILD)/tests/src-cpu-tsc
	$(BUILD)/tests/src-cpu-tsc
test-cpu-topology:
	@mkdir -p $(BUILD)/tests
	gcc -std=c11 -O2 -g -Wall -Wextra -Werror -fsanitize=undefined tests/cpu-topology.c kmodules/shared/cpu-topology.c -o $(BUILD)/tests/src-cpu-topology
	$(BUILD)/tests/src-cpu-topology
test-portability:
	$(MAKE) --no-print-directory BUILD="$(TEST_BUILD)" ISO="$(TEST_BUILD)/cloudberry.iso" KERNEL="$(TEST_BUILD)/kernel.elf" STAGE="$(TEST_BUILD)/src-iso-root" NVME_DISK="$(TEST_BUILD)/nvme.img" SATA_DISK="$(TEST_BUILD)/sata.img" _test-portability
_test-portability: $(ISO) $(BUILD)/tests/src-kernel.elf $(BUILD)/kmodules/probe.kmod test-cpu-topology test-cpu-tsc test-iommu-host
	python3 tests/mem-boot.py "$(abspath $(BUILD))" --cpus=4 --cpu=max,vendor=GenuineIntel --iommu=intel
	python3 tests/mem-boot.py "$(abspath $(BUILD))" --cpus=4 --cpu=max,vendor=AuthenticAMD --iommu=amd
	python3 tests/mem-boot.py "$(abspath $(BUILD))" --cpus=1 --cpu=max,vendor=GenuineIntel --iommu=amd
	python3 tests/mem-boot.py "$(abspath $(BUILD))" --cpus=1 --cpu=max,-avx
	python3 tests/mem-boot.py "$(abspath $(BUILD))" --cpus=1 --cpu=max,-xsave,-avx

.PHONY: test-iommu-backend
test-iommu-backend:
	@mkdir -p $(BUILD)/tests
	gcc -std=gnu11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fsanitize=undefined tests/iommu-amd-backend.c -o $(BUILD)/tests/src-iommu-amd-backend
	$(BUILD)/tests/src-iommu-amd-backend
test-iommu-host: test-iommu-backend

.PHONY: test-xstate-host
test-xstate-host:
	@mkdir -p $(BUILD)/tests
	gcc -std=gnu11 -O2 -g -Wall -Wextra -Werror -ffreestanding -mgeneral-regs-only -fsanitize=undefined tests/xstate-host.c -o $(BUILD)/tests/src-xstate-host
	$(BUILD)/tests/src-xstate-host
_test-portability: test-xstate-host

.PHONY: test-backend-contracts
test-backend-contracts:
	python3 tests/backend-contracts.py
_test-portability: test-backend-contracts

$(BUILD)/kmodules/xhci.elf: $(BUILD)/kmodules/xhci-pic.o $(BUILD)/kmodules/xhci.exports.o kmodules/shared/module.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@

.PHONY: test-xhci
test-xhci:
	@mkdir -p $(TEST_BUILD)/tests
	gcc -std=gnu11 -O2 -g -Wall -Wextra -Werror -fsanitize=undefined tests/xhci.c -o $(TEST_BUILD)/tests/src-xhci
	$(TEST_BUILD)/tests/src-xhci

$(BUILD)/kmodules/probe-xhci-pic.o: tests/probe-xhci.c tests/probe.h kmodules/shared/xhci.h kmodules/shared/usb-host.h
	@mkdir -p $(@D)
	$(CC) $(MODULE_CFLAGS) -c $< -o $@
$(BUILD)/tests/src-xhci-boot$(XHCI_HOTPLUG).o: tests/xhci-boot.c tests/probe.h kmodules/shared/xhci.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(if $(filter 1,$(XHCI_HOTPLUG)),-DXHCI_HOTPLUG_TEST,) -c $< -o $@

.PHONY: test-xhci-boot _test-xhci-boot
test-xhci-boot:
	$(MAKE) --no-print-directory BUILD="$(TEST_BUILD)" ISO="$(TEST_BUILD)/cloudberry.iso" KERNEL="$(TEST_BUILD)/kernel.elf" STAGE="$(TEST_BUILD)/src-iso-root" NVME_DISK="$(TEST_BUILD)/nvme.img" SATA_DISK="$(TEST_BUILD)/sata.img" XHCI_HOTPLUG=1 _test-xhci-boot
_test-xhci-boot: $(ISO) $(BUILD)/tests/src-kernel.elf $(BUILD)/kmodules/probe.kmod
	python3 tests/mem-boot.py "$(abspath $(BUILD))" --cpus=$(QEMU_CPUS) --iommu=$(QEMU_IOMMU) --xhci-hotplug

# ACPI owns the interpreter and applies its checked local RELATIVE relocations
# before calling upstream code. No compiler ABI leaks into the public hub.
UACPI_ROOT := kmodules/shared/acpi/vendor/uacpi
ACPI_CFLAGS = $(MODULE_CFLAGS) -I$(UACPI_ROOT)/include -DUACPI_USE_BUILTIN_STRING
UACPI_OBJECTS = $(patsubst $(UACPI_ROOT)/source/%.c,$(BUILD)/kmodules/acpi/uacpi-%.o,$(wildcard $(UACPI_ROOT)/source/*.c))
ACPI_OBJECTS = $(addprefix $(BUILD)/kmodules/acpi/,acpi.o runtime.o association.o) $(UACPI_OBJECTS) $(BUILD)/kmodules/acpi.exports.o
$(BUILD)/kmodules/acpi/%.o: kmodules/shared/acpi/%.c kmodules/shared/acpi/runtime.h $(MODULE_HEADERS) Makefile
	@mkdir -p $(@D)
	$(CC) $(ACPI_CFLAGS) -c $< -o $@
$(BUILD)/kmodules/acpi/uacpi-%.o: $(UACPI_ROOT)/source/%.c $(wildcard $(UACPI_ROOT)/include/uacpi/*.h) Makefile
	@mkdir -p $(@D)
	$(CC) $(ACPI_CFLAGS) -c $< -o $@
$(BUILD)/kmodules/acpi.elf: $(ACPI_OBJECTS) kmodules/shared/acpi/module.ld
	$(CC) -nostdlib -pie -Wl,--no-dynamic-linker -Wl,-Bsymbolic -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/acpi/module.ld $(filter %.o,$^) -o $@
$(BUILD)/kmodules/platform.elf: $(BUILD)/kmodules/platform-pic.o $(BUILD)/kmodules/platform.exports.o kmodules/shared/module.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@

.PHONY: test-acpi test-acpi-boot _test-acpi-boot
test-acpi:
	@mkdir -p $(BUILD)/tests
	gcc -std=gnu11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fsanitize=undefined tests/acpi.c kmodules/shared/acpi/association.c kmodules/shared/platform.c -o $(BUILD)/tests/src-acpi
	$(BUILD)/tests/src-acpi
test-acpi-boot:
	$(MAKE) --no-print-directory BUILD="$(TEST_BUILD)" ISO="$(TEST_BUILD)/cloudberry.iso" KERNEL="$(TEST_BUILD)/kernel.elf" STAGE="$(TEST_BUILD)/src-iso-root" NVME_DISK="$(TEST_BUILD)/nvme.img" SATA_DISK="$(TEST_BUILD)/sata.img" _test-acpi-boot
_test-acpi-boot: $(ISO) $(BUILD)/tests/src-kernel.elf $(BUILD)/kmodules/probe.kmod
	python3 tests/mem-boot.py "$(abspath $(BUILD))" --cpus=$(QEMU_CPUS) --iommu=$(QEMU_IOMMU) --accel=$(QEMU_ACCEL) --cpu="$(QEMU_CPU)" --acpi-fixture
$(BUILD)/tests/src-acpi-boot.o: tests/acpi-boot.c $(MODULE_HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/kmodules/acpi.kmod: $(BUILD)/kmodules/acpi.elf tools/check-acpi-image.py
	$(OBJCOPY) -O binary $< $@
	python3 tools/check-acpi-image.py $< $@

$(BUILD)/kmodules/usb.elf: $(BUILD)/kmodules/usb-pic.o $(BUILD)/kmodules/usb-descriptors-pic.o $(BUILD)/kmodules/usb.exports.o kmodules/shared/module.ld tools/check-module-pic.py
	python3 tools/check-module-pic.py $(filter %.o,$^)
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@

.PHONY: test-usb
test-usb:
	@mkdir -p $(TEST_BUILD)/tests
	gcc -std=gnu11 -O2 -g -Wall -Wextra -Werror -fsanitize=undefined tests/usb.c kmodules/shared/usb-descriptors.c -o $(TEST_BUILD)/tests/src-usb
	$(TEST_BUILD)/tests/src-usb

$(BUILD)/kmodules/probe-usb-pic.o: tests/probe-usb.c tests/probe.h kmodules/shared/usb.h
	@mkdir -p $(@D)
	$(CC) $(MODULE_CFLAGS) -c $< -o $@
$(BUILD)/tests/src-usb-boot$(USB_HOTPLUG)-hub$(USB_HUB_TEST)-storage$(STORAGE_TEST)-write$(STORAGE_WRITE_TEST).o: tests/usb-boot.c tests/probe.h kmodules/shared/usb.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(if $(filter 1 2,$(USB_HOTPLUG)),-DUSB_HOTPLUG_TEST=$(USB_HOTPLUG),) $(if $(filter 1,$(USB_HUB_TEST)),-DUSB_HUB_TEST,) $(if $(filter 1,$(STORAGE_TEST)),-DSTORAGE_TEST,) $(if $(filter 1,$(STORAGE_WRITE_TEST)),-DSTORAGE_WRITE_TEST,) -c $< -o $@
.PHONY: test-usb-boot _test-usb-boot
test-usb-boot:
	$(MAKE) --no-print-directory BUILD="$(TEST_BUILD)" ISO="$(TEST_BUILD)/cloudberry.iso" KERNEL="$(TEST_BUILD)/kernel.elf" STAGE="$(TEST_BUILD)/src-iso-root" NVME_DISK="$(TEST_BUILD)/nvme.img" SATA_DISK="$(TEST_BUILD)/sata.img" USB_HOTPLUG=1 _test-usb-boot
_test-usb-boot: $(ISO) $(BUILD)/tests/src-kernel.elf $(BUILD)/kmodules/probe.kmod
	python3 tests/mem-boot.py "$(abspath $(BUILD))" --cpus=$(QEMU_CPUS) --iommu=$(QEMU_IOMMU) --accel=$(QEMU_ACCEL) --cpu="$(QEMU_CPU)" --usb-hotplug

$(BUILD)/kmodules/usb-hub.elf: $(BUILD)/kmodules/usb-hub-pic.o $(BUILD)/kmodules/usb-hub.exports.o kmodules/shared/module.ld tools/check-module-pic.py
	python3 tools/check-module-pic.py $(filter %.o,$^)
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@

$(BUILD)/kmodules/probe-hubs-pic.o: tests/probe-hubs.c tests/probe.h kmodules/shared/usb-hub.h
	@mkdir -p $(@D)
	$(CC) $(MODULE_CFLAGS) -c $< -o $@
.PHONY: test-usb-hub-boot _test-usb-hub-boot
.PHONY: test-usb-hub
test-usb-hub:
	@mkdir -p $(TEST_BUILD)/tests
	gcc -std=gnu11 -O2 -g -Wall -Wextra -Werror -fsanitize=undefined tests/usb-hub.c -o $(TEST_BUILD)/tests/src-usb-hub
	$(TEST_BUILD)/tests/src-usb-hub
test-usb-hub-boot:
	$(MAKE) --no-print-directory BUILD="$(TEST_BUILD)" ISO="$(TEST_BUILD)/cloudberry.iso" KERNEL="$(TEST_BUILD)/kernel.elf" STAGE="$(TEST_BUILD)/src-iso-root" NVME_DISK="$(TEST_BUILD)/nvme.img" SATA_DISK="$(TEST_BUILD)/sata.img" USB_HUB_TEST=1 _test-usb-hub-boot
_test-usb-hub-boot: $(ISO) $(BUILD)/tests/src-kernel.elf $(BUILD)/kmodules/probe.kmod
	python3 tests/mem-boot.py "$(abspath $(BUILD))" --cpus=$(QEMU_CPUS) --iommu=$(QEMU_IOMMU) --accel=$(QEMU_ACCEL) --cpu="$(QEMU_CPU)" --usb-hubs

$(BUILD)/kmodules/config.elf: $(BUILD)/kmodules/config-pic.o $(BUILD)/kmodules/config.exports.o kmodules/shared/module.ld tools/check-module-pic.py
	python3 tools/check-module-pic.py $(filter %.o,$^)
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@

.PHONY: test-config
test-config:
	@mkdir -p $(TEST_BUILD)/tests
	gcc -std=gnu11 -O2 -g -Wall -Wextra -Werror -fsanitize=undefined tests/config.c -o $(TEST_BUILD)/tests/src-config
	$(TEST_BUILD)/tests/src-config

.PHONY: test-config-boot _test-config-boot
test-config-boot:
	$(MAKE) --no-print-directory BUILD="$(TEST_BUILD)" ISO="$(TEST_BUILD)/cloudberry.iso" KERNEL="$(TEST_BUILD)/kernel.elf" STAGE="$(TEST_BUILD)/src-iso-root" NVME_DISK="$(TEST_BUILD)/nvme.img" SATA_DISK="$(TEST_BUILD)/sata.img" _test-config-boot
_test-config-boot: $(ISO) $(BUILD)/tests/src-kernel.elf $(BUILD)/kmodules/probe.kmod
	python3 tests/mem-boot.py "$(abspath $(BUILD))" --cpus=$(QEMU_CPUS) --iommu=$(QEMU_IOMMU) --accel=$(QEMU_ACCEL) --cpu="$(QEMU_CPU)"
	python3 tests/mem-boot.py "$(abspath $(BUILD))" --config-error=missing
	python3 tests/mem-boot.py "$(abspath $(BUILD))" --config-error=duplicate
	python3 tests/mem-boot.py "$(abspath $(BUILD))" --config-error=invalid

.PHONY: test-boot-image _test-boot-image
test-boot-image:
	$(MAKE) --no-print-directory BUILD="$(TEST_BUILD)" ISO="$(TEST_BUILD)/cloudberry.iso" KERNEL="$(TEST_BUILD)/kernel.elf" STAGE="$(TEST_BUILD)/src-iso-root" NVME_DISK="$(TEST_BUILD)/nvme.img" SATA_DISK="$(TEST_BUILD)/sata.img" _test-boot-image
_test-boot-image: $(ISO)
	python3 tests/boot-image.py "$(abspath $(ISO))"

$(BUILD)/kmodules/usb-storage.elf: $(BUILD)/kmodules/usb-storage-pic.o $(BUILD)/kmodules/usb-storage.exports.o kmodules/shared/module.ld tools/check-module-pic.py
	python3 tools/check-module-pic.py $(filter %.o,$^)
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@

$(BUILD)/kmodules/partition.elf: $(BUILD)/kmodules/partition-pic.o $(BUILD)/kmodules/partition.exports.o kmodules/shared/module.ld tools/check-module-pic.py
	python3 tools/check-module-pic.py $(filter %.o,$^)
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@

$(BUILD)/kmodules/ext4-fs.elf: $(BUILD)/kmodules/ext4-fs-pic.o $(BUILD)/kmodules/ext4-fs.exports.o kmodules/shared/module.ld tools/check-module-pic.py
	python3 tools/check-module-pic.py $(filter %.o,$^)
	$(CC) -nostdlib -static -no-pie -Wl,--gc-sections -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@

.PHONY: test-usb-storage
test-usb-storage:
	@mkdir -p $(TEST_BUILD)/tests
	gcc -std=gnu11 -O2 -g -Wall -Wextra -Werror -fsanitize=undefined tests/usb-storage.c -o $(TEST_BUILD)/tests/src-usb-storage
	$(TEST_BUILD)/tests/src-usb-storage

$(TEST_BUILD)/tests/src-storage-fs: tests/storage-fs.c kmodules/shared/partition.h kmodules/shared/ext4-fs.h kmodules/shared/partition.c kmodules/shared/ext4-fs.c kmodules/shared/block.h kmodules/shared/block-util.h
	@mkdir -p $(@D)
	gcc -std=gnu11 -O2 -g -Wall -Wextra -Wno-error -fsanitize=undefined $(EXT4_ENGINE_FLAGS) $(filter %.c,$^) -Wl,--gc-sections -o $@

.PHONY: test-storage-fs
test-storage-fs: $(TEST_BUILD)/tests/src-storage-fs
	python3 tests/storage-fs.py

$(BUILD)/kmodules/probe-storage-pic.o: tests/probe-storage.c tests/probe.h kmodules/shared/usb-storage.h kmodules/shared/ext4-fs.h
	@mkdir -p $(@D)
	$(CC) $(MODULE_CFLAGS) -c $< -o $@

.PHONY: test-storage-boot _test-storage-boot
test-storage-boot:
	$(MAKE) --no-print-directory BUILD="$(TEST_BUILD)" ISO="$(TEST_BUILD)/cloudberry.iso" KERNEL="$(TEST_BUILD)/kernel.elf" STAGE="$(TEST_BUILD)/src-iso-root" NVME_DISK="$(TEST_BUILD)/nvme.img" SATA_DISK="$(TEST_BUILD)/sata.img" STORAGE_TEST=1 _test-storage-boot
_test-storage-boot: $(ISO) $(BUILD)/tests/src-kernel.elf $(BUILD)/kmodules/probe.kmod
	python3 tests/mem-boot.py "$(abspath $(BUILD))" --cpus=$(QEMU_CPUS) --iommu=$(QEMU_IOMMU) --accel=$(QEMU_ACCEL) --cpu="$(QEMU_CPU)" --storage $(if $(filter 1 2,$(USB_HOTPLUG)),--storage-hotplug,) $(if $(filter 2,$(USB_HOTPLUG)),--storage-nested,)

# Pinned allocation/journal engine, privately linked into ext4-fs.kmod.
EXT4_VENDOR := kmodules/shared/ext4-fs/vendor/lwext4
EXT4_LIBRARY_SOURCES := $(filter-out $(EXT4_VENDOR)/src/ext4_mkfs.c $(EXT4_VENDOR)/src/ext4_mbr.c $(EXT4_VENDOR)/src/ext4_xattr.c $(EXT4_VENDOR)/src/ext4_debug.c,$(wildcard $(EXT4_VENDOR)/src/*.c))
EXT4_ENGINE_FLAGS := -ffunction-sections -fdata-sections -DCONFIG_USE_DEFAULT_CFG=1 -DCONFIG_USE_USER_MALLOC=1 -DCONFIG_HAVE_OWN_ERRNO=1 -DCONFIG_DEBUG_PRINTF=0 -DCONFIG_DEBUG_ASSERT=0 -DCONFIG_XATTR_ENABLE=0 -include kmodules/shared/ext4-fs/runtime.h -I$(EXT4_VENDOR)/include
EXT4_ENGINE_OBJECTS := $(patsubst $(EXT4_VENDOR)/src/%.c,$(BUILD)/kmodules/ext4-fs/%.o,$(EXT4_LIBRARY_SOURCES)) $(BUILD)/kmodules/ext4-fs/engine.o $(BUILD)/kmodules/ext4-fs/strings.o
$(BUILD)/kmodules/ext4-fs/%.o: $(EXT4_VENDOR)/src/%.c kmodules/shared/ext4-fs/runtime.h Makefile
	@mkdir -p $(@D)
	$(CC) $(MODULE_CFLAGS) $(EXT4_ENGINE_FLAGS) -Ikmodules/shared/ext4-fs/compat -Wno-error -c $< -o $@
$(BUILD)/kmodules/ext4-fs/engine.o: kmodules/shared/ext4-fs/engine.c kmodules/shared/ext4-fs/engine.h kmodules/shared/ext4-fs/runtime.h Makefile
	@mkdir -p $(@D)
	$(CC) $(MODULE_CFLAGS) $(EXT4_ENGINE_FLAGS) -Ikmodules/shared/ext4-fs/compat -c $< -o $@
$(BUILD)/kmodules/ext4-fs/strings.o: kmodules/shared/ext4-fs/strings.c Makefile
	@mkdir -p $(@D)
	$(CC) $(MODULE_CFLAGS) -ffunction-sections -fdata-sections -Ikmodules/shared/ext4-fs/compat -c $< -o $@
$(BUILD)/kmodules/ext4-fs.elf: $(EXT4_ENGINE_OBJECTS)

$(TEST_BUILD)/tests/src-storage-fs: $(EXT4_LIBRARY_SOURCES) kmodules/shared/ext4-fs/engine.c kmodules/shared/ext4-fs/engine.h

$(TEST_BUILD)/tests/src-ext4-write: tests/ext4-write.c tests/storage-fs.c kmodules/shared/partition.c kmodules/shared/ext4-fs.c kmodules/shared/ext4-fs/engine.c $(EXT4_LIBRARY_SOURCES) kmodules/shared/block.h kmodules/shared/ext4-fs.h
	@mkdir -p $(@D)
	gcc -std=gnu11 -O1 -g -Wall -Wextra -Wno-error -fsanitize=undefined $(EXT4_ENGINE_FLAGS) $(filter-out tests/storage-fs.c,$(filter %.c,$^)) -Wl,--gc-sections -o $@

.PHONY: test-ext4-write test-storage-write-boot _test-storage-write-boot
test-ext4-write: $(TEST_BUILD)/tests/src-ext4-write
	python3 tests/ext4-write.py

test-storage-write-boot:
	$(MAKE) --no-print-directory BUILD="$(TEST_BUILD)" ISO="$(TEST_BUILD)/cloudberry.iso" KERNEL="$(TEST_BUILD)/kernel.elf" STAGE="$(TEST_BUILD)/src-iso-root" NVME_DISK="$(TEST_BUILD)/nvme.img" SATA_DISK="$(TEST_BUILD)/sata.img" STORAGE_TEST=1 STORAGE_WRITE_TEST=1 _test-storage-write-boot
_test-storage-write-boot: $(ISO) $(BUILD)/tests/src-kernel.elf $(BUILD)/kmodules/probe.kmod
	python3 tests/mem-boot.py "$(abspath $(BUILD))" --cpus=$(QEMU_CPUS) --iommu=$(QEMU_IOMMU) --accel=$(QEMU_ACCEL) --cpu="$(QEMU_CPU)" --storage --storage-write $(if $(filter 1 2,$(USB_HOTPLUG)),--storage-hotplug,) $(if $(filter 2,$(USB_HOTPLUG)),--storage-nested,)

.PHONY: test-ext4-runtime
test-ext4-runtime:
	@mkdir -p $(TEST_BUILD)/tests
	gcc -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fsanitize=undefined tests/ext4-runtime.c -o $(TEST_BUILD)/tests/src-ext4-runtime
	$(TEST_BUILD)/tests/src-ext4-runtime

-include $(EXT4_ENGINE_OBJECTS:.o=.d)
EXT4_ENGINE_HEADERS := $(wildcard $(EXT4_VENDOR)/include/*.h $(EXT4_VENDOR)/include/misc/*.h) kmodules/shared/ext4-fs/runtime.h kmodules/shared/ext4-fs/engine.h
$(TEST_BUILD)/tests/src-storage-fs $(TEST_BUILD)/tests/src-ext4-write: $(EXT4_ENGINE_HEADERS)

$(BUILD)/kmodules/sata.elf: $(BUILD)/kmodules/sata-pic.o $(BUILD)/kmodules/sata.exports.o kmodules/shared/module.ld tools/check-module-pic.py
	python3 tools/check-module-pic.py $(filter %.o,$^)
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@

BLOCK_BACKEND ?= nvme
ifeq ($(filter $(BLOCK_BACKEND),nvme sata),)
$(error BLOCK_BACKEND must be nvme or sata)
endif
$(BUILD)/kmodules/probe-block-pic.o: tests/probe-block.c tests/probe.h kmodules/shared/sata.h kmodules/shared/nvme.h
	@mkdir -p $(@D)
	$(CC) $(MODULE_CFLAGS) -c $< -o $@
$(BUILD)/tests/src-block-main.o: kernel/main.c Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -DBOOT_BLOCK_TEST=1 -c $< -o $@
$(BUILD)/tests/src-block-$(BLOCK_BACKEND).o: tests/block-boot.c tests/probe.h Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -DBLOCK_BACKEND_TEST=$(if $(filter nvme,$(BLOCK_BACKEND)),1,2) -c $< -o $@
$(BUILD)/tests/src-block-kernel.elf: FORCE $(BUILD)/tests/src-block-main.o $(BUILD)/tests/src-block-$(BLOCK_BACKEND).o $(COMMON_OBJECTS) boot/linker.ld
	$(CC) $(LDFLAGS) $(filter %.o,$^) -o $@
.PHONY: test-block-boot _test-block-boot
test-block-boot:
	$(MAKE) --no-print-directory BUILD="$(TEST_BUILD)" ISO="$(TEST_BUILD)/cloudberry.iso" KERNEL="$(TEST_BUILD)/kernel.elf" STAGE="$(TEST_BUILD)/src-iso-root" _test-block-boot
_test-block-boot: $(ISO) $(BUILD)/tests/src-block-kernel.elf $(BUILD)/kmodules/probe.kmod
	python3 tests/block-boot.py "$(abspath $(BUILD))" --backend=$(BLOCK_BACKEND) $(if $(filter 1,$(BLOCK_MULTI)),--multi,) --cpus=$(QEMU_CPUS) --iommu=$(QEMU_IOMMU) --accel=$(QEMU_ACCEL) --cpu="$(QEMU_CPU)"

.PHONY: test-hub
test-hub:
	@mkdir -p $(TEST_BUILD)/tests
	gcc -std=gnu11 -O2 -g -Wall -Wextra -Werror -ffunction-sections -fdata-sections -pthread tests/hub.c kernel/mediate.S -Wl,--gc-sections -o $(TEST_BUILD)/tests/src-hub
	$(TEST_BUILD)/tests/src-hub

$(BUILD)/tests/src-hub-main.o: kernel/main.c kernel/surfaces.h Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -DBOOT_HUB_TEST=1 -c $< -o $@
$(BUILD)/tests/src-hub-boot.o: tests/hub-boot.c tests/probe.h kernel/surfaces.h Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/tests/src-hub-kernel.elf: FORCE $(BUILD)/tests/src-hub-main.o $(BUILD)/tests/src-hub-boot.o $(COMMON_OBJECTS) boot/linker.ld
	$(CC) $(LDFLAGS) $(filter %.o,$^) -o $@
.PHONY: test-hub-boot _test-hub-boot
test-hub-boot:
	$(MAKE) --no-print-directory BUILD="$(TEST_BUILD)" ISO="$(TEST_BUILD)/cloudberry.iso" KERNEL="$(TEST_BUILD)/kernel.elf" STAGE="$(TEST_BUILD)/src-iso-root" _test-hub-boot
_test-hub-boot: $(ISO) $(BUILD)/tests/src-hub-kernel.elf $(BUILD)/kmodules/probe.kmod
	python3 tests/block-boot.py "$(abspath $(BUILD))" --backend=nvme --hub-only --cpus=$(QEMU_CPUS) --iommu=$(QEMU_IOMMU) --accel=$(QEMU_ACCEL) --cpu="$(QEMU_CPU)"

$(BUILD)/kmodules/bootstrap.elf: $(BUILD)/kmodules/bootstrap-apps-pic.o $(BUILD)/kmodules/bootstrap-load-pic.o $(BUILD)/kmodules/bootstrap-pic.o $(BUILD)/kmodules/bootstrap.exports.o kmodules/shared/module.ld tools/check-module-pic.py
	python3 tools/check-module-pic.py $(filter %.o,$^)
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@

$(BUILD)/tests/src-bootstrap-main.o: kernel/main.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -DBOOT_BOOTSTRAP_TEST -c $< -o $@
$(BUILD)/tests/src-bootstrap-test.o: tests/bootstrap-boot.c kmodules/shared/bootstrap.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/tests/src-bootstrap-kernel.elf: $(BUILD)/tests/src-bootstrap-main.o $(BUILD)/tests/src-bootstrap-test.o $(COMMON_OBJECTS) boot/linker.ld FORCE
	$(CC) $(LDFLAGS) $(filter %.o,$^) -o $@
.PHONY: test-bootstrap-boot _test-bootstrap-boot
test-bootstrap-boot:
	$(MAKE) --no-print-directory BUILD="$(TEST_BUILD)" ISO="$(TEST_BUILD)/cloudberry.iso" KERNEL="$(TEST_BUILD)/kernel.elf" STAGE="$(TEST_BUILD)/src-iso-root" _test-bootstrap-boot
_test-bootstrap-boot: $(ISO) $(BUILD)/tests/src-bootstrap-kernel.elf $(BUILD)/kmodules/probe.kmod
	python3 tests/block-boot.py "$(abspath $(BUILD))" --bootstrap --backend=$(BOOTSTRAP_BACKEND) $(if $(filter 1,$(BLOCK_MULTI)),--multi,) $(if $(filter 1,$(BOOTSTRAP_DUPLICATE)),--duplicate,) --cpus=$(QEMU_CPUS) --iommu=$(QEMU_IOMMU) --accel=$(QEMU_ACCEL) --cpu="$(QEMU_CPU)"
BOOTSTRAP_BACKEND ?= usb

# Explicit disk-only test images; excluded from candidate prerequisites/staging.
$(BUILD)/kmodules/future-pic.o: tests/future.c kmodules/shared/module-load.h
	@mkdir -p $(@D)
	$(CC) $(MODULE_CFLAGS) -c $< -o $@
$(BUILD)/kmodules/future-fail-pic.o: tests/future-fail.c kmodules/shared/module-load.h
	@mkdir -p $(@D)
	$(CC) $(MODULE_CFLAGS) -c $< -o $@
$(BUILD)/kmodules/future.exports.S: tests/future.json tools/module-exports.py
	python3 tools/module-exports.py $< $@
$(BUILD)/kmodules/future-fail.exports.S: tests/future-fail.json tools/module-exports.py
	python3 tools/module-exports.py $< $@
$(BUILD)/kmodules/future.elf: $(BUILD)/kmodules/future-pic.o $(BUILD)/kmodules/future.exports.o kmodules/shared/module.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@
$(BUILD)/kmodules/future-fail.elf: $(BUILD)/kmodules/future-fail-pic.o $(BUILD)/kmodules/future-fail.exports.o kmodules/shared/module.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@
.PHONY: test-module-disk-boot _test-module-disk-boot
test-module-disk-boot:
	$(MAKE) --no-print-directory BUILD="$(TEST_BUILD)" ISO="$(TEST_BUILD)/cloudberry.iso" KERNEL="$(TEST_BUILD)/kernel.elf" STAGE="$(TEST_BUILD)/src-iso-root" _test-module-disk-boot
_test-module-disk-boot: $(ISO) $(BUILD)/tests/src-bootstrap-kernel.elf $(BUILD)/kmodules/probe.kmod $(BUILD)/kmodules/future.kmod $(BUILD)/kmodules/future-fail.kmod
	python3 tests/block-boot.py "$(abspath $(BUILD))" --bootstrap --disk-modules $(if $(filter 1,$(DISK_MODULES_SUCCESS)),--disk-success,) --backend=usb --cpus=$(QEMU_CPUS) --iommu=$(QEMU_IOMMU) --accel=$(QEMU_ACCEL) --cpu="$(QEMU_CPU)"
.PHONY: test-driver-manifest
test-driver-manifest:
	@mkdir -p $(TEST_BUILD)/tests
	gcc -std=gnu11 -O2 -g -Wall -Wextra -Werror -fsanitize=undefined -ffunction-sections -fdata-sections tests/bootstrap-load.c -Wl,--gc-sections -o $(TEST_BUILD)/tests/src-driver-manifest
	$(TEST_BUILD)/tests/src-driver-manifest

$(BUILD)/src-boot-spaces.o: boot/spaces.c boot/spaces.h boot/memory.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/src-spaces.o: kernel/spaces.c kernel/spaces.h kernel/cpu.h boot/spaces.h kmodules/shared/mem-spaces.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: test-spaces
test-spaces:
	@mkdir -p $(TEST_BUILD)/tests
	gcc -std=gnu11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fno-strict-aliasing -fsanitize=undefined tests/spaces.c boot/spaces.c boot/memory.c -o $(TEST_BUILD)/tests/src-spaces
	$(TEST_BUILD)/tests/src-spaces

$(BUILD)/kmodules/probe-spaces-pic.o: tests/probe-spaces.c tests/probe.h kmodules/shared/mem-spaces.h
	@mkdir -p $(@D)
	$(CC) $(MODULE_CFLAGS) -c $< -o $@
$(BUILD)/tests/src-spaces-main.o: kernel/main.c Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -DBOOT_SPACES_TEST -c $< -o $@
$(BUILD)/tests/src-spaces-test.o: tests/spaces-boot.c kmodules/shared/mem-spaces.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/tests/src-spaces-kernel.elf: $(BUILD)/tests/src-spaces-main.o $(BUILD)/tests/src-spaces-test.o $(COMMON_OBJECTS) boot/linker.ld FORCE
	$(CC) $(LDFLAGS) $(filter %.o,$^) -o $@
.PHONY: test-spaces-boot _test-spaces-boot
test-spaces-boot:
	$(MAKE) --no-print-directory BUILD="$(TEST_BUILD)" ISO="$(TEST_BUILD)/cloudberry.iso" KERNEL="$(TEST_BUILD)/kernel.elf" STAGE="$(TEST_BUILD)/src-iso-root" _test-spaces-boot
_test-spaces-boot: $(ISO) $(BUILD)/tests/src-spaces-kernel.elf $(BUILD)/kmodules/probe.kmod
	python3 tests/block-boot.py "$(abspath $(BUILD))" --backend=usb --spaces-only --cpus=$(QEMU_CPUS) --iommu=$(QEMU_IOMMU) --accel=$(QEMU_ACCEL) --cpu="$(QEMU_CPU)"

$(BUILD)/kmodules/space-fault-pic.o: tests/space-fault.c tests/space-fault.h
	@mkdir -p $(@D)
	$(CC) $(MODULE_CFLAGS) -c $< -o $@
$(BUILD)/kmodules/space-fault-entry.o: tests/space-fault-entry.S
	@mkdir -p $(@D)
	$(CC) $(MODULE_CFLAGS) -c $< -o $@

$(BUILD)/kmodules/process-entry.o: kmodules/shared/process-entry.S
	@mkdir -p $(@D)
	$(CC) $(MODULE_CFLAGS) -c $< -o $@
$(BUILD)/kmodules/process.elf: $(BUILD)/kmodules/process-pic.o $(BUILD)/kmodules/process-entry.o $(BUILD)/kmodules/process.exports.o kmodules/shared/module.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@

$(BUILD)/kmodules/probe-process-pic.o: tests/probe-process.c tests/probe.h kmodules/shared/process.h
	@mkdir -p $(@D)
	$(CC) $(MODULE_CFLAGS) -c $< -o $@
$(BUILD)/tests/apps/process-probe.elf: tests/apps/process.c sdk/application.ld
	@mkdir -p $(@D)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-stack-protector -fno-pie -mno-red-zone -mgeneral-regs-only -nostdlib -static -no-pie -Wl,--build-id=none,-z,noexecstack,-z,max-page-size=4096,-T,sdk/application.ld $< -o $@
$(BUILD)/tests/src-process-main.o: kernel/main.c Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -DBOOT_PROCESS_TEST -c $< -o $@
$(BUILD)/tests/src-process-test.o: tests/process-boot.c tests/probe.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/tests/src-process-kernel.elf: $(BUILD)/tests/src-process-main.o $(BUILD)/tests/src-process-test.o $(COMMON_OBJECTS) boot/linker.ld FORCE
	$(CC) $(LDFLAGS) $(filter %.o,$^) -o $@
.PHONY: test-process-boot _test-process-boot
test-process-boot:
	$(MAKE) --no-print-directory BUILD="$(TEST_BUILD)" ISO="$(TEST_BUILD)/cloudberry.iso" KERNEL="$(TEST_BUILD)/kernel.elf" STAGE="$(TEST_BUILD)/src-iso-root" _test-process-boot
_test-process-boot: $(BUILD)/tests/apps/handle-bench.elf $(BUILD)/tests/apps/keyboard-fault.elf $(BUILD)/tests/apps/keyboard-client.elf $(BUILD)/tests/apps/handles-probe.elf $(ISO) $(BUILD)/tests/src-process-kernel.elf $(BUILD)/tests/apps/process-probe.elf $(BUILD)/kmodules/probe.kmod
	python3 tests/block-boot.py "$(abspath $(BUILD))" --backend=usb --process-only --cpus=$(QEMU_CPUS) --iommu=$(QEMU_IOMMU) --accel=$(QEMU_ACCEL) --cpu="$(QEMU_CPU)"

.PHONY: test-process-elf
test-process-elf:
	@mkdir -p $(BUILD)/tests
	gcc -std=gnu11 -O2 -Wall -Wextra -Werror -fsanitize=undefined tests/process-elf.c -o $(BUILD)/tests/process-elf
	$(BUILD)/tests/process-elf

$(BUILD)/kmodules/handles.elf: $(BUILD)/kmodules/handles-pic.o $(BUILD)/kmodules/handles.exports.o kmodules/shared/module.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@

$(BUILD)/tests/apps/handles-probe.elf: tests/apps/handles.c sdk/system.c sdk/system.h interfaces/system.h sdk/application.ld
	@mkdir -p $(@D)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-stack-protector -fno-pie -mno-red-zone -mgeneral-regs-only -nostdlib -static -no-pie -Wl,--build-id=none,-z,noexecstack,-z,max-page-size=4096,-T,sdk/application.ld tests/apps/handles.c sdk/system.c -o $@
.PHONY: test-manifest
test-manifest:
	@mkdir -p $(BUILD)/tests
	gcc -std=gnu11 -O2 -Wall -Wextra -Werror -fsanitize=undefined tests/manifest.c -o $(BUILD)/tests/manifest
	$(BUILD)/tests/manifest

$(BUILD)/kmodules/io.elf: $(BUILD)/kmodules/io-pic.o $(BUILD)/kmodules/io.exports.o kmodules/shared/module.ld
	$(CC) -nostdlib -static -no-pie -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-T,kmodules/shared/module.ld $(filter %.o,$^) -o $@
.PHONY: test-handles
test-handles:
	@mkdir -p $(BUILD)/tests
	gcc -std=gnu11 -O2 -Wall -Wextra -Werror -ffreestanding -fsanitize=undefined tests/handles.c -o $(BUILD)/tests/handles
	$(BUILD)/tests/handles

$(BUILD)/apps/keyboard.elf: handlers/keyboard/keyboard.c handlers/keyboard/hid.c handlers/keyboard/hid.h handlers/keyboard/startup.h sdk/system.c sdk/system.h interfaces/keyboard.h interfaces/io.h sdk/application.ld
	@mkdir -p $(@D)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-stack-protector -fno-pie -mno-red-zone -mgeneral-regs-only -nostdlib -static -no-pie -Wl,--build-id=none,-z,noexecstack,-z,max-page-size=4096,-T,sdk/application.ld handlers/keyboard/keyboard.c handlers/keyboard/hid.c sdk/system.c -o $@
$(BUILD)/src-handler-bundles/.ready: $(BUILD)/apps/keyboard.elf handlers/keyboard/manifest.json $(wildcard handlers/keyboard/layouts/*.json)
	rm -rf $(BUILD)/src-handler-bundles
	@mkdir -p $(BUILD)/src-handler-bundles/keyboard/layouts
	cp $(BUILD)/apps/keyboard.elf $(BUILD)/src-handler-bundles/keyboard/keyboard.elf
	cp handlers/keyboard/manifest.json $(BUILD)/src-handler-bundles/keyboard/manifest.json
	cp handlers/keyboard/layouts/*.json $(BUILD)/src-handler-bundles/keyboard/layouts/
	@touch $@
.PHONY: test-keyboard
test-keyboard:
	@mkdir -p $(BUILD)/tests
	gcc -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=undefined tests/keyboard.c handlers/keyboard/hid.c -o $(BUILD)/tests/keyboard
	$(BUILD)/tests/keyboard handlers/keyboard/layouts/ibm.json handlers/keyboard/layouts/abnt2.json
	gcc -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=undefined tests/keyboard-stream.c handlers/keyboard/hid.c -o $(BUILD)/tests/keyboard-stream
	$(BUILD)/tests/keyboard-stream

$(BUILD)/tests/apps/keyboard-client.elf: tests/apps/keyboard-client.c sdk/system.c sdk/system.h interfaces/keyboard.h sdk/application.ld
	@mkdir -p $(@D)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-stack-protector -fno-pie -mno-red-zone -mgeneral-regs-only -nostdlib -static -no-pie -Wl,--build-id=none,-z,noexecstack,-z,max-page-size=4096,-T,sdk/application.ld tests/apps/keyboard-client.c sdk/system.c -o $@

$(BUILD)/tests/apps/keyboard-fault.elf: tests/apps/keyboard-fault.c handlers/keyboard/keyboard.c handlers/keyboard/hid.c handlers/keyboard/hid.h sdk/system.c sdk/system.h interfaces/keyboard.h sdk/application.ld
	@mkdir -p $(@D)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-stack-protector -fno-pie -mno-red-zone -mgeneral-regs-only -nostdlib -static -no-pie -Wl,--build-id=none,-z,noexecstack,-z,max-page-size=4096,-T,sdk/application.ld tests/apps/keyboard-fault.c handlers/keyboard/hid.c sdk/system.c -o $@

$(BUILD)/tests/apps/handle-bench.elf: tests/apps/handle-bench.c tests/handle-bench.h sdk/system.c sdk/system.h interfaces/handle.h interfaces/io.h sdk/application.ld
	@mkdir -p $(@D)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-stack-protector -fno-pie -mno-red-zone -mgeneral-regs-only -nostdlib -static -no-pie -Wl,--build-id=none,-z,noexecstack,-z,max-page-size=4096,-T,sdk/application.ld tests/apps/handle-bench.c sdk/system.c -o $@

.PHONY: test-handle-report test-bootstrap-apps
test-handle-report:
	python3 tests/test-handle-report.py
test-bootstrap-apps:
	@mkdir -p $(BUILD)/tests
	gcc -std=gnu11 -O2 -Wall -Wextra -Werror -fsanitize=undefined tests/bootstrap-apps.c -o $(BUILD)/tests/bootstrap-apps
	$(BUILD)/tests/bootstrap-apps
