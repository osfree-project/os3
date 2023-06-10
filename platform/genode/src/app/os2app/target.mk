include $(REP_DIR)/mk/osfree.mk

TARGET = os2app
CC_CXX_WARN_STRICT =
SRC_CC = main.cc kal/thread2.cc
SRC_C  = initdone.c api/api.c kal/thread.c \
         kal/util.c kal/start.c kal/kal.c kal/dl.c
# disable dead code elimination
#CC_OPT += -O0 -fno-dce -fno-dse -fno-tree-dce -fno-tree-dse
LIBS = base libc compat os2srv os2fs os2exec handle

ifeq ($(filter-out $(SPECS),x86_32),)
	SRC_C += kal/arch/x86_32/tramp.c
endif
ifeq ($(filter-out $(SPECS),x86_64),)
	SRC_C += kal/arch/x86_64/tramp.c
endif
ifeq ($(filter-out $(SPECS),arm),)
	SRC_C += kal/arch/arm/tramp.c
endif

vpath %.c $(OS3_DIR)/shared/app/os2app

ifneq ($(INSTALL_DIR),)
all: map
endif

map: $(TARGET)
	@(cd $(CURDIR) && nm os2app | grep 'Kal' | awk '{printf "0x%s %s\n", $$1, $$3}' >os2app.1 && \
	wc -l os2app.1 | awk '{print $$1}' >kal.map && \
	cat os2app.1 >>kal.map)
	@cd ../../bin && ln -sf $(CURDIR)/kal.map kal.map
