.PHONY: all bluedbm ramdrive ramdrive_intr ramdrive_timing libdummy #dragonfire  
all: all bluedbm ramdrive ramdrive_intr ramdrive_timing libdummy #dragonfire

bluedbm:
	cd bluedbm; $(MAKE) 

ramdrive:
	cd ramdrive; $(MAKE)

ramdrive_intr:
	cd ramdrive_intr; $(MAKE)

ramdrive_timing:
	cd ramdrive_timing; $(MAKE)

libdummy:
	cd libdummy; $(MAKE)

dragonfire:
	cd dragonfire; $(MAKE)


.PHONY: clean 
clean: 
	@cd bluedbm; $(MAKE) clean
	@cd ramdrive; $(MAKE) clean
	@cd ramdrive_intr; $(MAKE) clean
	@cd ramdrive_timing; $(MAKE) clean
	@cd libdummy; $(MAKE) clean
	#@cd ramdrive_dragonfire; $(MAKE) clean
