include $(REP_DIR)/mk/osfree.mk

TARGET = os2exec
CC_CXX_WARN_STRICT = 
SRC_CC = main.cc ixfutil.cc api-local.cc
SRC_C  = ixfmgr.c modmgr.c api/api.c initdone.c
INC_DIR += $(OS3_DIR)/shared/server/os2exec/api
LIBS   = base libc compat memmgr os2srv os2fs

vpath %.c $(OS3_DIR)/shared/server/os2exec
