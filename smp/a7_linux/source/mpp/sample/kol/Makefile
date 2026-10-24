include ../Makefile.param

########### SRCS & INC ############
INC_FLAGS += -I./
INC_FLAGS += -I./state_machine
INC_FLAGS += -I./media
INC_FLAGS += -I./algorithm
INC_FLAGS += -I./osd
INC_FLAGS += -I./peripheral
INC_FLAGS += -I./dfx
INC_FLAGS += -I./scene
INC_FLAGS += -I./region
INC_FLAGS += -I../scene_auto/include
INC_FLAGS += -I../scene_auto/src/sample

AIDETECT_ENABLE ?= y
LIGHT_ENABLE ?= n
IR_CUT_ENABLE ?= n

ifeq ($(AIDETECT_ENABLE), y)
LIBS_LD_CFLAGS += $(REL_LIB)/libot_osal.a
LIBS_LD_CFLAGS += $(REL_LIB)/libss_mpi_aidetect.a
LIBS_LD_CFLAGS += $(REL_LIB)/libss_mpi_smartae.a
LIBS_LD_CFLAGS += $(REL_LIB)/libaiservice_comm.a
LIBS_LD_CFLAGS += $(REL_LIB)/libss_mpi_cipher.a
endif
LIBS_LD_CFLAGS += $(REL_LIB)/libss_mpi_syskol.a

SMP_SRCS := $(wildcard $(PWD)/*.c)
SMP_SRCS += $(wildcard $(PWD)/state_machine/*.c)
SMP_SRCS += $(wildcard $(PWD)/media/*.c)
SMP_SRCS += $(wildcard $(PWD)/algorithm/*.c)
SMP_SRCS += $(wildcard $(PWD)/osd/*.c)
SMP_SRCS += $(wildcard $(PWD)/peripheral/*.c)
SMP_SRCS += $(wildcard $(PWD)/dfx/*.c)
SMP_SRCS += $(wildcard $(PWD)/scene/*.c)
SMP_SRCS += $(wildcard $(PWD)/../scene_auto/src/core/*.c)
SMP_SRCS += $(wildcard $(PWD)/../scene_auto/src/sample/scene_loadparam.c)
SMP_SRCS += $(wildcard $(PWD)/region/*.c)

ifeq ($(AIDETECT_ENABLE), y)
	CFLAGS += -DAIDETECT_ENABLE
endif

ifeq ($(LIGHT_ENABLE), y)
	CFLAGS += -DLIGHT_ENABLE
endif

ifeq ($(IR_CUT_ENABLE), y)
	CFLAGS += -DIR_CUT_ENABLE
endif

TARGET := sample_kol

TARGET_PATH := $(PWD)

CFLAGS += $(INC_FLAGS)
# compile linux or LITEOS
include $(PWD)/../$(ARM_ARCH)_$(OSTYPE).mak
