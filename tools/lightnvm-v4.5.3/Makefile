CONFIG_MODULE_SIG=n

lightnvm-y := core.o sysblk.o gennvm.o rrpc.o

obj-m := lightnvm.o

ccflags-y := -I$(src)

export KROOT=/lib/modules/4.5.7-040507-generic/build

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
