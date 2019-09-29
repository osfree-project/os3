include $(REP_DIR)/build.cfg

cdefs = -D__l4env__

ifeq ($(filter-out $(ARCH),amd64),)
	cdefs += -D__64bit__
endif

cdefs += -I$(REP_DIR)/include
cdefs += -I$(OS3_DIR)/include/os2
cdefs += -I$(OS3_DIR)/include

CFLAGS += $(cdefs)
O=$(BLD_DIR)

SYMLINKS = $(ROOT)/cd/boot/grub/menu.lst \
	  $(OS3_DIR)/platform/l4env/os2 \
	  $(BLD_DIR)/bin/$(arch)/$(l4api)/os2 \
	  $(REP_DIR)/tftpboot \
	  $(REP_DIR)$(GRUB) \
	  $(BLD_DIR)/bin/$(arch)/$(l4api)/fiasco \
	  $(BLD_DIR)/bin/$(arch)/$(l4api)/fiasco_symbols \
	  $(BLD_DIR)/bin/$(arch)/$(l4api)/fiasco_lines \
	  $(BLD_DIR)/bin/$(arch)/$(l4api)/l4con

MOUNTFLAG = $(OS3_DIR)/platform/l4env/mounted.flg

BID_IGN_ROOT_CONF=y

.DEFAULT_GOAL := all

l4: mount
	chroot $(LENNY) $(MAKE) -C $(BLD_DIR) O=$(BLD_DIR) BID_IGN_ROOT_CONF= $(MAKEFLAGS) all

config-l4: mount
	chroot $(LENNY) $(MAKE) -C $(L4DIR) O=$(BLD_DIR) $(MAKEFLAGS) config

dice: mount
	chroot $(LENNY) /bin/sh -c "cd $(ROOT)/dice && ./configure && $(MAKE)"

fiasco: mount
	chroot $(LENNY) $(MAKE) -C $(FIASCO_BLD_DIR) $(MAKEFLAGS) all

config-fiasco: mount
	chroot $(LENNY) $(MAKE) -C $(FIASCO_DIR) O=$(FIASCO_BLD_DIR) $(MAKEFLAGS) config

os3: symlinks mount
	chroot $(LENNY) $(MAKE) -C `pwd` $(MAKEFLAGS) all

run: symlinks mount
ifeq ($(USE_OLD_QEMU),)
# newer qemu
	qemu-system-i386 \
		-no-kvm \
		$(GDB) \
		-m $(MEM) \
		-boot n \
		-netdev user,bootfile=$(GRUB),tftp=$(REP_DIR),id=net0 \
		-device ne2k_pci,netdev=net0 \
		-L $(OS3_DIR)/tools/bootrom \
		$(DISP) \
		-serial stdio \
	| tee $(REP_DIR)/qemu.log
else
# old qemu from Debian 5 Lenny
	chroot $(LENNY) qemu-system-i386 \
		-no-kqemu \
		$(GDB) \
		-m $(MEM) \
		-boot n \
		-tftp $(OS3_DIR)/platform/l4env \
		-bootp $(GRUB) \
		-net nic,model=ne2k_pci,vlan=0 \
		-net user,vlan=0 \
		-L $(OS3_DIR)/tools/bootrom \
		$(DISP) \
		-serial stdio \
	| tee $(REP_DIR)/qemu.log
endif

mount: $(MOUNTFLAG)

$(MOUNTFLAG):
	cd $(LENNY) && $(OS3_DIR)/platform/l4env/tools/mountall.sh mount $(MAIN)
	touch $@

unmount: $(MOUNTFLAG)
	cd $(LENNY) && $(OS3_DIR)/platform/l4env/tools/mountall.sh unmount $(MAIN) && rm -rf $<

symlinks: $(SYMLINKS)

$(ROOT)/cd/boot/grub/menu.lst: $(OS3_DIR)/platform/l4env/tools/menu.lst
	@if [ ! -d $(dir $@) ]; then \
	    mkdir -p $(dir $@); \
	fi
	@ln -sf $< $@

$(BLD_DIR)/bin/$(arch)/$(l4api)/os2: $(OS3_DIR)/platform/l4env/os2
	@if [ ! -d $(dir $@) ]; then \
	    mkdir -p $(dir $@); \
	fi
	@ln -sf $< $@

$(OS3_DIR)/platform/l4env/os2: $(OS3_DIR)/filesys/os2
	@if [ ! -d $(dir $@) ]; then \
	    mkdir -p $(dir $@); \
	fi
	@ln -sf $< $@

$(REP_DIR)/tftpboot: $(ROOT)
	@if [ ! -d $(dir $@) ]; then \
	    mkdir -p $(dir $@); \
	fi
	@ln -sf $< $@

$(REP_DIR)$(GRUB): $(OS3_DIR)$(GRUB)
	@if [ ! -d $(dir $@) ]; then \
	    mkdir -p $(dir $@); \
	fi
	@ln -sf $< $@

$(BLD_DIR)/bin/$(arch)/$(l4api)/fiasco: $(FIASCO_BLD_DIR)/main
	@if [ ! -d $(dir $@) ]; then \
	    mkdir -p $(dir $@); \
	fi
	@ln -sf $< $@

$(BLD_DIR)/bin/$(arch)/$(l4api)/fiasco_symbols: $(FIASCO_BLD_DIR)/Symbols
	@if [ ! -d $(dir $@) ]; then \
	    mkdir -p $(dir $@); \
	fi
	@ln -sf $< $@

$(BLD_DIR)/bin/$(arch)/$(l4api)/fiasco_lines: $(FIASCO_BLD_DIR)/Lines
	@if [ ! -d $(dir $@) ]; then \
	    mkdir -p $(dir $@); \
	fi
	@ln -sf $< $@

$(BLD_DIR)/bin/$(arch)/$(l4api)/l4con: $(BLD_DIR)/bin/$(arch)/$(l4api)/con
	@if [ ! -d $(dir $@) ]; then \
	    mkdir -p $(dir $@); \
	fi
	@ln -sf $< $@
