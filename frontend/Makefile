.PHONY: all kernel user
all: kernel user

kernel:
	cd kernel; $(MAKE) 

user:
	cd user; $(MAKE)

.PHONY: clean clean-kernel clean-user
clean: clean-kernel clean-user

clean-kernel:
	@cd kernel; $(MAKE) clean

clean-user:
	@cd user; $(MAKE) clean
