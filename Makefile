SHELL := /bin/bash
KERNEL_VERSION ?= $(shell uname --kernel-release)
KSRC := /lib/modules/$(KERNEL_VERSION)/build
VERSION := 2.0.0

INSTALL_DIR := /lib/modules/$(KERNEL_VERSION)/kernel/drivers/platform/x86
MODDEST_DIR := /lib/modules/$(KERNEL_VERSION)/kernel/drivers/platform/x86
DKMS_DIR := /usr/src/LenovoLegion-$(VERSION)

obj-m := lenovo_legion.o
lenovo_legion-objs := legion-dkms.o \
                      legion-rapl-mmio.o \
                      legion-intel-msr.o \
                      legion-intel-msr-sysfs.o

all:
	$(MAKE) -C $(KSRC) M=$(shell pwd) modules

allWarn:
	$(MAKE) -C $(KSRC) M=$(shell pwd) KCFLAGS=-W modules

clean:
	$(MAKE) -C $(KSRC) M=$(shell pwd) clean

install: all
	@rm --force --verbose $(INSTALL_DIR)/lenovo_legion.ko
	@mkdir --parent --verbose $(MODDEST_DIR)
	@install --preserve-timestamps -D --mode=644 *.ko $(INSTALL_DIR)
	@depmod --all $(KERNEL_VERSION)
	@printf "%s\n" "Installation finished."

uninstall:
	@rm -f $(INSTALL_DIR)/lenovo_legion.ko
	@depmod --all
	@printf "%s\n" "Uninstall finished."

dkms: clean
	dkms remove -m LenovoLegion -v $(VERSION) --all 2>/dev/null || true
	rm -rf $(DKMS_DIR)
	mkdir -p $(DKMS_DIR)
	cp -R . $(DKMS_DIR)/
	dkms add -m LenovoLegion -v $(VERSION)
	dkms build -m LenovoLegion -v $(VERSION) --force
	dkms install -m LenovoLegion -v $(VERSION) --force
	modprobe -r lenovo_legion 2>/dev/null || true
	modprobe lenovo_legion

uninstall-dkms:
	dkms remove -m LenovoLegion -v $(VERSION) --all 2>/dev/null || true
	rm -rf $(DKMS_DIR)
