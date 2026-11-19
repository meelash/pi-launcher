#
# Pi Launcher: a bare-metal boot menu for the Raspberry Pi 3 B+ that starts
# other bare-metal programs (Typing Adventure, Graphing Calculator, ...).
#
#   make            build the menu, the programs and the SD card image (build/pi-apps.img)
#   make sdcard     just the files for the card (build/sdcard/), to copy onto a FAT32 card
#   make kernel     only the menu kernel (build/sdcard/kernel8.img)
#   make test       run the desktop simulator tests (screenshots in build/shots/)
#   make qemu       QEMU variant of the whole card, programs included (scripts/qemu-run.sh)
#   make debug      menu kernel with extra USB logging (build/debug/kernel8.img)
#   make clean      remove build output (keeps the downloaded toolchain)
#
# Programs come from sibling checkouts, built with their own Makefiles:
#   APPS = <image name>:<repo directory> ...
# or from ready-made kernels: make sdcard PREBUILT=<dir with typing.img, ...>
#

SHELL      := /bin/bash
export PATH := $(PATH):/usr/sbin:/sbin

ROOT       := $(CURDIR)
BUILD      := $(ROOT)/build
CIRCLE     := $(ROOT)/external/circle

APPS       ?= typing:$(ROOT)/../pi-typing graphcalc:$(ROOT)/../pi-graphcalc
PREBUILT   ?=

# Arm GNU toolchain recommended by Circle (downloaded into tools/, checksum verified).
TC_NAME    := arm-gnu-toolchain-15.2.rel1-x86_64-aarch64-none-elf
TC_URL     := https://developer.arm.com/-/media/Files/downloads/gnu/15.2.rel1/binrel/$(TC_NAME).tar.xz
TC_SHA256  := 66f7ce7c1bf662f589a4caf440812375f3cd8000a033ccf0971127a0726d6921
TC_PREFIX  := $(ROOT)/tools/$(TC_NAME)/bin/aarch64-none-elf-

# Raspberry Pi GPU firmware, pinned to the revision Circle is tested with.
FW_REV     := 0641c5bf25d185d5bf25d8dcafd1da6dc8fdbf68
FW_URL     := https://github.com/raspberrypi/firmware/raw/$(FW_REV)/boot
FW_FILES   := bootcode.bin start.elf fixup.dat LICENCE.broadcom

# Only the USB keyboard driver is compiled in; every other USB class is left out.
# --kernel-max-size also limits the size of the programs the menu can start.
CIRCLE_OPTS := -r 3 --keymap US --kernel-max-size 8 \
	-d EXCLUDE_USB_STORAGE -d EXCLUDE_USB_MOUSE -d EXCLUDE_USB_GAMEPAD \
	-d EXCLUDE_USB_PRINTER -d EXCLUDE_USB_NET -d EXCLUDE_USB_BLUETOOTH \
	-d EXCLUDE_USB_MIDI -d EXCLUDE_USB_AUDIO -d EXCLUDE_USB_SERIAL \
	-d EXCLUDE_USB_TOUCHSCREEN
CIRCLE_LIBS := lib lib/usb lib/input lib/fs addon/SDCard addon/fatfs

IMAGE      := $(BUILD)/pi-apps.img
IMAGE_MB   := 64

.PHONY: all sdcard kernel apps firmware toolchain circle test sim qemu debug clean
all: $(IMAGE)

# --- toolchain -------------------------------------------------------------
toolchain: $(TC_PREFIX)gcc
$(TC_PREFIX)gcc:
	mkdir -p tools
	curl -sSfL -o tools/$(TC_NAME).tar.xz $(TC_URL)
	echo "$(TC_SHA256)  tools/$(TC_NAME).tar.xz" | sha256sum -c -
	tar -C tools -xf tools/$(TC_NAME).tar.xz
	rm tools/$(TC_NAME).tar.xz
	touch $@

# --- Circle ----------------------------------------------------------------
# external/circle stays pristine. Each build variant gets its own copy of it,
# with our fixes from patches/ applied, rebuilt whenever a patch changes.
CIRCLE_REV   = $(shell git -C $(CIRCLE) rev-parse HEAD)
FIX_PATCHES := $(sort $(wildcard patches/circle-fix-*.patch))
DBG_PATCHES := $(FIX_PATCHES) $(sort $(wildcard patches/circle-debug-*.patch))

$(CIRCLE)/Rules.mk:
	git submodule update --init external/circle

# $(call build_circle,<copy dir>,<extra configure options>,<patches>)
define build_circle
	rm -rf $(1)
	git clone -q $(CIRCLE) $(1)
	git -C $(1) checkout -q $(CIRCLE_REV)
	for p in $(3); do git -C $(1) apply $(ROOT)/$$p || exit 1; done
	cd $(1) && ./configure $(CIRCLE_OPTS) $(2) -p $(TC_PREFIX) -f
	for d in $(CIRCLE_LIBS); do $(MAKE) -C $(1)/$$d || exit 1; done
endef

circle: $(BUILD)/circle-pi.stamp
$(BUILD)/circle-pi.stamp: $(TC_PREFIX)gcc $(CIRCLE)/Rules.mk $(FIX_PATCHES)
	$(call build_circle,$(BUILD)/circle-pi,,$(FIX_PATCHES))
	touch $@
$(BUILD)/circle-qemu.stamp: $(TC_PREFIX)gcc $(CIRCLE)/Rules.mk $(FIX_PATCHES)
	$(call build_circle,$(BUILD)/circle-qemu,--qemu,$(FIX_PATCHES))
	touch $@
$(BUILD)/circle-debug.stamp: $(TC_PREFIX)gcc $(CIRCLE)/Rules.mk $(DBG_PATCHES)
	$(call build_circle,$(BUILD)/circle-debug,,$(DBG_PATCHES))
	touch $@

# --- menu kernel -----------------------------------------------------------
kernel: $(BUILD)/circle-pi.stamp
	rm -f src/pi/kernel8.*  # shared output of all variants: always relink
	$(MAKE) -C src/pi CIRCLEHOME=$(BUILD)/circle-pi BUILD=$(BUILD)/pi
	mkdir -p $(BUILD)/sdcard
	cp src/pi/kernel8.img $(BUILD)/sdcard/

# --- programs --------------------------------------------------------------
# $(call app_name,typing:../pi-typing) -> typing ; app_dir -> ../pi-typing
app_name = $(word 1,$(subst :, ,$(1)))
app_dir  = $(word 2,$(subst :, ,$(1)))

# Builds each program with its own Makefile (<target> = kernel or qemu) and
# copies its kernel into <dest>/apps/<name>.img.
# $(call build_apps,<target>,<kernel path in the program's build dir>,<dest>)
define build_apps
	mkdir -p $(3)/apps
	cp apps.txt $(3)/apps/
	if [ -n "$(PREBUILT)" ]; then \
		cp $(PREBUILT)/*.img $(3)/apps/; \
	else \
		for a in $(APPS); do \
			name=$${a%%:*} dir=$${a#*:}; \
			[ -f $$dir/Makefile ] || { echo "missing $$dir (set APPS or PREBUILT)"; exit 1; }; \
			$(MAKE) -C $$dir $(1) || exit 1; \
			cp $$dir/build/$(2) $(3)/apps/$$name.img || exit 1; \
		done; \
	fi
endef

apps:
	$(call build_apps,kernel,sdcard/kernel8.img,$(BUILD)/sdcard)

# --- firmware --------------------------------------------------------------
firmware: $(BUILD)/firmware/.verified
$(BUILD)/firmware/.verified: firmware/SHA256SUMS
	mkdir -p $(BUILD)/firmware
	for f in $(FW_FILES); do \
		[ -f $(BUILD)/firmware/$$f ] || curl -sSfL -o $(BUILD)/firmware/$$f $(FW_URL)/$$f || exit 1; \
	done
	cd $(BUILD)/firmware && sha256sum -c $(ROOT)/firmware/SHA256SUMS
	touch $@

# --- SD card ---------------------------------------------------------------
sdcard: kernel apps firmware
	cp $(addprefix $(BUILD)/firmware/,$(FW_FILES)) boot/config.txt boot/cmdline.txt $(BUILD)/sdcard/
	@echo "SD card files in build/sdcard:"; ls -l $(BUILD)/sdcard $(BUILD)/sdcard/apps

# A small MBR disk with one FAT32 partition holding only the files above.
$(IMAGE): sdcard
	rm -f $@
	truncate -s $(IMAGE_MB)M $@
	printf 'label: dos\nstart=2048, type=c\n' | sfdisk -q $@
	mkfs.vfat -F 32 --offset 2048 -n PI-APPS $@ >/dev/null
	mcopy -s -i $@@@1M $(BUILD)/sdcard/* ::/
	@echo "Image ready: $@ ($(IMAGE_MB) MB). Flash it with Raspberry Pi Imager or dd."

# --- desktop simulator and QEMU --------------------------------------------
sim:
	$(MAKE) -C src/sim

test:
	tests/sim/run.sh

# QEMU needs Circle built with --qemu (different SD card controller), for the
# menu and for every program. The card image is rebuilt each time.
qemu: $(BUILD)/circle-qemu.stamp
	rm -f src/pi/kernel8.*  # shared output of all variants: always relink
	$(MAKE) -C src/pi CIRCLEHOME=$(BUILD)/circle-qemu BUILD=$(BUILD)/qemu-obj
	rm -rf $(BUILD)/qemu/card && mkdir -p $(BUILD)/qemu/card
	mv src/pi/kernel8.img $(BUILD)/qemu/
	$(call build_apps,qemu,qemu/kernel8.img,$(BUILD)/qemu/card)
	rm -f $(BUILD)/qemu/sd.img && truncate -s 64M $(BUILD)/qemu/sd.img
	printf 'label: dos\nstart=2048, type=c\n' | sfdisk -q $(BUILD)/qemu/sd.img
	mkfs.vfat -F 32 --offset 2048 $(BUILD)/qemu/sd.img >/dev/null
	mcopy -s -i $(BUILD)/qemu/sd.img@@1M $(BUILD)/qemu/card/* ::/
	@echo "Run: scripts/qemu-run.sh (then scripts/qemu-drive.py build/qemu shot:screen)"

# Same as the menu kernel, plus the USB logging patches and debug-level logging.
debug: $(BUILD)/circle-debug.stamp
	rm -f src/pi/kernel8.*  # shared output of all variants: always relink
	$(MAKE) -C src/pi CIRCLEHOME=$(BUILD)/circle-debug BUILD=$(BUILD)/debug-obj EXTRA_DEFINES=-DDIAG_LOG_LEVEL=LogDebug
	mkdir -p $(BUILD)/debug && mv src/pi/kernel8.img $(BUILD)/debug/
	@echo "Debug kernel: build/debug/kernel8.img (replace kernel8.img on the card)"

clean:
	rm -rf $(BUILD)/{circle-*,pi,qemu*,debug*,sdcard,gen,sim,shots,simcard} $(IMAGE) src/pi/kernel8.*
