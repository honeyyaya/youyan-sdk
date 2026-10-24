include $(PWD)/../Makefile.param

########### SRCS & INC ############
SMP_SRCS := $(wildcard *.c)
SMP_SRCS += $(wildcard $(PWD)/../ai_common/*.c)

TARGET := sample_aidetect_vie

TARGET_PATH := $(PWD)

CFLAGS += -I$(PWD)/../ai_common

LIBS_LD_CFLAGS += -L$(REL_LIB) -lsecurec -lss_mpi -lss_mpi_sysmem -lsvp_acl -lprotobuf-c -lss_mpi_aidetect \
                  -laiservice_comm -lss_mpi_cipher -lss_mpi_ive -lot_osal
# compile linux or LITEOS
include $(PWD)/../../../$(ARM_ARCH)_$(OSTYPE).mak
