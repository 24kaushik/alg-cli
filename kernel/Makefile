ifneq ($(KERNELRELEASE),)

obj-m += alg_rgb.o
ccflags-y += -I$(src)/../include

else

KVER ?= $(shell uname -r)
KDIR ?= /usr/lib/modules/$(KVER)/build
MODULE_DIR := $(CURDIR)

.PHONY: all clean

all:
	@test -f "$(KDIR)/Makefile" || { \
		echo "Kernel headers not found at $(KDIR)" >&2; \
		exit 1; \
	}
	$(MAKE) -C "$(KDIR)" M="$(MODULE_DIR)" modules

clean:
	@if test -f "$(KDIR)/Makefile"; then \
		$(MAKE) -C "$(KDIR)" M="$(MODULE_DIR)" clean; \
	fi

endif
