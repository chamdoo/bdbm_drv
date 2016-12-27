CONFIG_MODULE_SIG=n

nvme-y := core.o pci.o scsi.o lightnvm.o

obj-m := nvme.o

ccflags-y := -I$(src)

KBUILD_EXTRA_SYMBOLS := $(PWD)/../../devices/dumbssd/Module.symvers

export KROOT=/lib/modules/$(shell uname -r)/build

.PHONY: default
default: modules

.PHONY: modules
modules:
	@$(MAKE) -C $(KROOT) M=$(PWD) modules

.PHONY: modules_check
modules_check:
	@$(MAKE) -C $(KROOT) C=2 M=$(PWD) modules

.PHONY: modules_install
modules_install:
	@$(MAKE) -C $(KROOT) M=$(PWD) modules_install

.PHONY: kernel_clean
kernel_clean:
	@$(MAKE) -C $(KROOT) M=$(PWD) clean
 
.PHONY: clean
clean: kernel_clean
	@rm -rf Module.markers modules.order
	@rm -rf *.o .*.cmd
