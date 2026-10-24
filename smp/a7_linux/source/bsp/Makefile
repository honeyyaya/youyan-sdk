##########################################################################################
#	prepare param
##########################################################################################
export OSDRV_DIR=$(shell pwd)
export SDK_TOP_DIR=$(OSDRV_DIR)/../../../..

export TEE_ENABLE?=0
SECURE_BOOT_ENABLE?=0
export DEBUG?=1
export KOL?=0

export FASTBOOT_ENABLE ?= 0
COMPILE_TIME := $(shell date "+%G-%m-%d %H:%M:%S")
COMMIT_ID := $(shell git log -n 1 | grep "commit" | awk '{print $$2}')
AUTHOR := $(shell whoami)

ifeq ($(OSDRV_DIR)/../open_source, $(wildcard $(OSDRV_DIR)/../open_source))
OPEN_SOURCE=$(OSDRV_DIR)/../open_source
else ifeq ($(OSDRV_DIR)/../../open_source, $(wildcard $(OSDRV_DIR)/../../open_source))
OPEN_SOURCE=$(OSDRV_DIR)/../../open_source
else ifeq ($(OSDRV_DIR)/../../../../open_source, $(wildcard $(OSDRV_DIR)/../../../../open_source))
OPEN_SOURCE=$(OSDRV_DIR)/../../../../open_source
else ifeq ($(OSDRV_DIR)/../../../../../open_source, $(wildcard $(OSDRV_DIR)/../../../../../open_source))
OPEN_SOURCE=$(OSDRV_DIR)/../../../../../open_source
else ifeq ($(OSDRV_DIR)/open_source, $(wildcard $(OSDRV_DIR)/open_source))
OPEN_SOURCE=$(OSDRV_DIR)/open_source
else
$(warning "---------------------------------------------------------------------")
$(warning "   Cannot found the open_source Contents!!       ")
$(warning "---------------------------------------------------------------------")
$(error )
endif

ifeq ($(OSDRV_DIR)/../out/, $(wildcard $(OSDRV_DIR)/../out/))
OUT_DIR=$(OSDRV_DIR)/../out
else
$(warning "---------------------------------------------------------------------")
$(warning "   Cannot found the out Contents!!       ")
$(warning "---------------------------------------------------------------------")
$(error )
endif


MAKE=make
LIB_TYPE ?= musl
export LLVM ?=
export CHIP?=hi3516cv610
export CHIP_FAMILY=hi3516cv610
export MINI_COMPILE ?=
ifeq ($(LIB_TYPE), musl)
export OSDRV_CROSS ?= arm-v01c02-linux-musleabi
else ifeq ($(LIB_TYPE), glibc)
export OSDRV_CROSS ?= arm-v01c02-linux-gnueabi
else
$(warning "---------------------------------------------------------------------")
$(warning "   LIB_TYPE Input Is ERR!!       ")
$(warning "---------------------------------------------------------------------")
$(error )
endif

ifneq ($(LLVM), )
export CROSS_COMPILE=
else
export CROSS_COMPILE=$(OSDRV_CROSS)-
endif
export CPU_TYPE :=
export BUILD :=all
ARCH_TYPE ?= arm
export ARCH :=arm
export ARM_ARCH_VERSION := armv7
AMP_TYPE ?= linux
GDB_COMPILE ?= y

BOOT_MEDIA ?= spi

ifeq ($(BOOT_MEDIA), spi)
MEDIUM_FLAG = _
endif
ifeq ($(BOOT_MEDIA), spi_nand)
MEDIUM_FLAG = _
endif
ifeq ($(BOOT_MEDIA), emmc)
MEDIUM_FLAG = _emmc_
endif

ifeq ($(TEE_ENABLE),1)
export TEE_FLAG := _tee
else
export TEE_FLAG :=
endif

ifeq ($(DEBUG),1)
BSP_DEBUG_FLAG = debug_
endif

ifeq ($(KOL),1)
BSP_KOL_FLAG = kol_
KOL_REG_BIN_24M=_24M
endif

ifeq ($(CHIP),hi3516cv610)
REGBIN_XLSM ?= Hi3516CV610-DMEB_4L_DDR3_2133M_128MB_16bit-A7_950M_QFN$(KOL_REG_BIN_24M).xlsm
else ifeq ($(CHIP),hi3516cv608)
REGBIN_XLSM ?= Hi3516CV608-DMEB_4L_DDR2_1333M_64MB_16bit-A7_950M_QFN$(KOL_REG_BIN_24M).xlsm
else
$(error you must set valid CHIP: hi3516cv610, hi3516cv608)
endif

OPTEE_DIR:=$(OPEN_SOURCE)/optee
export BOOT_IMAGE_TOOL_DIR=$(OSDRV_DIR)/tools/pc/image_tool
export BOOT_IMAGE_TOOL_INPUT_DIR=$(BOOT_IMAGE_TOOL_DIR)/input
BOOT_IMAGE_TOOL_OUTPUT_FILE=$(BOOT_IMAGE_TOOL_DIR)/image/oem/boot_image.bin
TEE_IMAGE_TOOL_OUTPUT_FILE=$(BOOT_IMAGE_TOOL_DIR)/image/oem/tee_image.bin

ifeq ($(TEE_ENABLE),1)
IMAGE_TOOL_SCRIPT=$(BOOT_IMAGE_TOOL_DIR)/oem/oem_quick_build_tee.py
else ifeq ($(SECURE_BOOT_ENABLE),1)
IMAGE_TOOL_SCRIPT=$(BOOT_IMAGE_TOOL_DIR)/oem/oem_quick_build_secure_boot.py
else
IMAGE_TOOL_SCRIPT=$(BOOT_IMAGE_TOOL_DIR)/oem/oem_quick_build.py
endif


export KERNEL_CFG:=$(CHIP)$(MEDIUM_FLAG)$(BSP_KOL_FLAG)$(BSP_DEBUG_FLAG)defconfig

TOOLCHAIN_VERSION-musl=CS71.2.10.5.B002
TOOLCHAIN_VERSION-glibc=V12CS61.007.010

TOOLCHAIN_VERSION?=$(TOOLCHAIN_VERSION-$(LIB_TYPE))

TOOLCHAIN_FILE:= $(shell which $(OSDRV_CROSS)-gcc )
TOOLCHAIN_DIR:=$(shell dirname $(shell dirname $(TOOLCHAIN_FILE)))
RUNTIMELIB_DIR=$(shell dirname $(TOOLCHAIN_DIR))/$(RUNTIME_LIB)

CROSS_SPECIFIED:=y
TOOLCHAIN_RUNTIME_LIB:=runtime_lib

ifneq ($(MINI_COMPILE),1)
BUSYBOX_CFG ?= config_$(BSP_DEBUG_FLAG)arm_a7_softfp_neon
else
BUSYBOX_CFG ?= config_mini_arm_a7_softfp_neon
endif

export UBOOT_CONFIG:=$(CHIP)$(MEDIUM_FLAG)$(BSP_DEBUG_FLAG)defconfig

TOOLCHAIN_RUNTIME_LIB_C:=lib.tgz

SECURE_BOOT:=secureboot_release
SECURE_BOOT_TAR:=$(SECURE_BOOT).tgz
CROSS_CC :=

CROSS_CC = $(OSDRV_CROSS)-gcc
GCC_INSTALL_TOOL:=$(TOOLCHAIN_DIR)/tools/install_dynamic_basic_library.sh

BUSYBOX_DIR := $(OPEN_SOURCE)/busybox

KERNEL_DIR:=$(OPEN_SOURCE)/linux

UBOOT_DIR:=$(OPEN_SOURCE)/u-boot

XZ_DIR:= $(OPEN_SOURCE)/xz

LZMA_DIR:=$(OPEN_SOURCE)/lzma

EUDEV_DIR:=$(OPEN_SOURCE)/eudev

MTD_DIR:=$(OPEN_SOURCE)/mtd-utils/

ZLIB_DIR:= $(OPEN_SOURCE)/zlib

SQUASHFS_DIR:=$(OPEN_SOURCE)/squashfs

ETHTOOL_DIR:= $(OPEN_SOURCE)/ethtool

GZIP_DIR:= $(OPEN_SOURCE)/gzip

VENDOR_TOOLS:=tools/board/reg-tools-1.0.0

REG_BIN:=reg_info.bin
BOOT_REG_BIN:=$(CHIP)_$(REG_BIN)
JFFS2_IMAGE_BIN_64K:=rootfs_$(CHIP)_64k.jffs2
JFFS2_IMAGE_BIN_128K:=rootfs_$(CHIP)_128k.jffs2
JFFS2_IMAGE_BIN_256K:=rootfs_$(CHIP)_256k.jffs2

EMMC_ROOTFS_SIZE ?= 96
E2FSPROGS_DIR:=$(OPEN_SOURCE)/e2fsprogs/
EXT4_TOOL:=mkfs.ext4
EXT4_IMAGE_BIN:=rootfs_$(CHIP)_$(EMMC_ROOTFS_SIZE)M.ext4
COUNT_SIZE := $(shell echo $$(( $(EMMC_ROOTFS_SIZE) * 1024 * 2 )))

REGBING_PACKAGE_FILE:=RegBin
REGBING_HEAD_MAGIC=2b8c6e1a1a6e8c2b

STR_FIRMWARE_DIR := $(OSDRV_DIR)/components/str_firmware

UBOOT_ENV_DIR := $(OSDRV_DIR)/tools/pc/uboot_env
##########################################################################################
#	prepare regbin tools
##########################################################################################
REGBING_PACKAGE = $(shell find $(OSDRV_DIR)/tools/pc/boot_tools/ -maxdepth 1 -type f -regextype egrep -iregex ".*RegBin.*\.tgz" | head -n 1)

ifeq ($(REGBING_PACKAGE), )
$(warning "---------------------------------------------------------------------")
$(warning "   Cannot found the REGBIN package !!       ")
$(warning "---------------------------------------------------------------------")
$(error )
endif

TARGET_XLSM := $(REGBIN_XLSM)
export PUB_BOARD:=board_$(BSP_DEBUG_FLAG)$(LIB_TYPE)_$(ARCH_TYPE)$(TEE_FLAG)
export PUB_IMAGE:=$(CHIP)$(MEDIUM_FLAG)image_$(BSP_DEBUG_FLAG)$(LIB_TYPE)$(TEE_FLAG)
export PUB_ROOTFS:=rootfs_$(BSP_DEBUG_FLAG)$(LIB_TYPE)_$(ARCH_TYPE)$(TEE_FLAG)

export BK_FONT = "\033[1;30m"
export RD_FONT = "\033[1;31m"
export GN_FONT = "\033[1;32m"
export YE_FONT = "\033[1;33m"
export BU_FONT = "\033[1;34m"
export PL_FONT = "\033[1;35m"
export CY_FONT = "\033[1;36m"
export WH_FONT = "\033[1;37m"
export DONE    = "\033[0m"

##########################################################################################
#	set task
##########################################################################################
all: toolchain_version_check prepare boot gslboot_build kernel rootfs_prepare secure_libs busybox pctools boardtools rootfs_build teeos tee_client uboot_env
clean: boot_clean gslboot_clean kernel_clean \
       busybox_clean pctools_clean boardtools_clean \
       rootfs_clean uboot_env_clean\
       secure_libs_clean teeos_clean tee_client_clean

distclean:clean pub_clean
a:=$(shell $(OSDRV_CROSS)-gcc --version)
b:=$(findstring $(TOOLCHAIN_VERSION),$(a))
c:=$(word 4, $(a))
##########################################################################################
#	toolchain version check
##########################################################################################
toolchain_version_check:
	@echo "---------    toolchain_version_check"
ifneq ($(b),$(TOOLCHAIN_VERSION))
	$(warning "---------------------------------------------------------------------")
	$(warning "       < !!! Attention:please check toolchain version !!! >          ")
	$(warning "         The current toolchain version is $(c)  ")
	$(warning "         But requested toolchain version is ($(TOOLCHAIN_VERSION))")
	$(warning "         Please make sure the toolchain version is the best match    ")
	$(warning "---------------------------------------------------------------------")
	$(shell read -p "Press Enter to continue compile; Ctrl + C to stop it !!" )
endif

##########################################################################################
#	prepare
##########################################################################################
prepare: toolchain_version_check
	@echo -e $(GN_FONT)" ########## prepare ########## "$(DONE)	
	@mkdir -p $(BOOT_IMAGE_TOOL_INPUT_DIR)
	mkdir $(OSDRV_DIR)/pub/$(PUB_IMAGE) -p
	mkdir $(OSDRV_DIR)/pub/bin/$(PUB_BOARD) -p
	mkdir $(OSDRV_DIR)/pub/bin/pc -p

##########################################################################################
#	regbin_prepare
##########################################################################################
regbin_prepare:
	@echo -e $(GN_FONT)" ########## regbin_prepare ########## "$(DONE)
	@echo -e $(CY_FONT)" #TARGET_XLSM: $(TARGET_XLSM) "$(DONE)
	rm $(OSDRV_DIR)/tools/pc/boot_tools/$(REGBING_PACKAGE_FILE) -rf
	tar xzf $(REGBING_PACKAGE) -C $(OSDRV_DIR)/tools/pc/boot_tools
	chmod 777 $(OSDRV_DIR)/tools/pc/boot_tools/$(REGBING_PACKAGE_FILE)/regbin
	cp $(OSDRV_DIR)/tools/pc/boot_tools/$(TARGET_XLSM) $(OSDRV_DIR)/tools/pc/boot_tools/$(REGBING_PACKAGE_FILE)
	cd $(OSDRV_DIR)/tools/pc/boot_tools/$(REGBING_PACKAGE_FILE) && ./regbin $(TARGET_XLSM) $(BOOT_REG_BIN) -magic $(REGBING_HEAD_MAGIC);cd -
	mv $(OSDRV_DIR)/tools/pc/boot_tools/$(REGBING_PACKAGE_FILE)/$(BOOT_REG_BIN) $(OSDRV_DIR)/tools/pc/boot_tools
	rm $(OSDRV_DIR)/tools/pc/boot_tools/$(REGBING_PACKAGE_FILE) -rf

notools_prepare:
	pushd $(OSDRV_DIR)/pub;tar xzf $(PUB_ROOTFS).tgz;popd

##########################################################################################
#	build uboot
##########################################################################################
boot: prepare regbin_prepare gzip_pc
	@echo -e $(GN_FONT)" ########## build boot ########## "$(DONE)

	$(MAKE) -C $(UBOOT_DIR) CHIP=$(CHIP) CHIP_FAMILY=$(CHIP_FAMILY) BOOT_MEDIA=$(BOOT_MEDIA) LIB_TYPE=$(LIB_TYPE) \
			ARCH_TYPE=$(ARCH_TYPE) all

boot_clean:
	@echo -e $(RD_FONT)" ########## boot distclean ########## "$(DONE)
	rm $(OSDRV_DIR)/tools/pc/boot_tools/*$(REG_BIN) -rf
	$(MAKE) -C $(UBOOT_DIR) distclean

str_firmware:
ifeq ($(STR_FIRMWARE_DIR), $(wildcard $(STR_FIRMWARE_DIR)))
	$(MAKE) -C $(STR_FIRMWARE_DIR)
endif

str_firmware_clean:
ifeq ($(STR_FIRMWARE_DIR), $(wildcard $(STR_FIRMWARE_DIR)))
	$(MAKE) -C $(STR_FIRMWARE_DIR) clean
endif

##########################################################################################
#	build kernel
##########################################################################################
ifeq ($(KOL),1)
kernel: prepare gzip_pc str_firmware
else
kernel: prepare gzip_pc
endif
	@echo -e $(GREEN)"--------- build kernel"  $(DONE)
	$(MAKE) -C $(KERNEL_DIR) CHIP=$(CHIP) BOOT_MEDIA=$(BOOT_MEDIA) LIB_TYPE=$(LIB_TYPE) TEE_ENABLE=$(TEE_ENABLE) \
				ARCH_TYPE=$(ARCH_TYPE) all
ifeq ($(FASTBOOT_ENABLE),1)
	cp $(OPEN_SOURCE)/gzip/bin/gzip gzip
	chmod +x gzip
	chmod +x $(OSDRV_DIR)/tools/pc/compress/kernel_compress.sh
	cp $(OSDRV_DIR)/pub/$(PUB_IMAGE)/uImage uImage
	$(OSDRV_DIR)/tools/pc/compress/kernel_compress.sh uImage -p
	mv uImage.gz $(OSDRV_DIR)/pub/$(PUB_IMAGE)/uImage
	rm gzip
endif

ifeq ($(KOL),1)
kernel_clean: str_firmware_clean
else
kernel_clean:
endif
	@echo -e $(RD_FONT)" ########## kernel distclean ########## "$(DONE)
	$(MAKE) -C $(KERNEL_DIR) clean

##########################################################################################
# build secure libs
##########################################################################################
secure_libs: prepare
	@echo -e $(GN_FONT)" ########## build secure libs ########## "$(DONE)
	pushd $(OSDRV_DIR)/components/secure_c/src;make clean;CC=gcc make lib CHECK_OPTION=check; popd
	mkdir $(OSDRV_DIR)/components/secure_c/libs -p
	cp $(OSDRV_DIR)/components/secure_c/lib/libsecurec.a $(OSDRV_DIR)/components/secure_c/libs/libsecurec-pc.a
	pushd $(OSDRV_DIR)/components/secure_c/src;make clean;CC=$(CROSS_CC) make lib CHECK_OPTION=check;popd
	cp $(OSDRV_DIR)/components/secure_c/lib/libsecurec.a $(OSDRV_DIR)/components/secure_c/libs/libsecurec-board.a
	pushd $(OSDRV_DIR)/components/secure_c/src;make clean;popd

secure_libs_clean:
	@echo -e $(RD_FONT)" ########## secure_libs_clean ########## "$(DONE)
	pushd $(OSDRV_DIR)/components/secure_c/src;make clean;popd

##########################################################################################
#	prepare rootfs
##########################################################################################
rootfs_prepare: prepare
	@echo -e $(GN_FONT)" ########## prepare rootfs ########## "$(DONE)
ifeq ($(OSDRV_DIR)/pub/$(PUB_ROOTFS), $(wildcard $(OSDRV_DIR)/pub/$(PUB_ROOTFS)))
	pushd $(OSDRV_DIR)/pub/$(PUB_ROOTFS); chmod +w usr/bin; chmod +w usr/sbin; chmod +w sbin; popd
	rm $(OSDRV_DIR)/pub/$(PUB_ROOTFS)* -rf
endif
	make -C $(OSDRV_DIR)/rootfs_scripts/ all
	cp -r $(OSDRV_DIR)/rootfs_scripts/rootfs/ $(OSDRV_DIR)/pub/$(PUB_ROOTFS)
ifneq ($(LLVM),)
	$(LLVM_INSTALL_TOOL) TOOLCHAIN_PATH=$(LLVM_TOOLCHAIN_PATH) INSTALL_PATH=$(OSDRV_DIR)/pub/$(PUB_ROOTFS) TARGET=$(LLVM_DEFAULT_TARGET)
else
	echo $(TOOLCHAIN_DIR)/$(TOOLCHAIN_RUNTIME_LIB)   $(TOOLCHAIN_RUNTIME_LIB_C)
	$(GCC_INSTALL_TOOL) $(TOOLCHAIN_DIR) $(OSDRV_DIR)/pub/$(PUB_ROOTFS) a7_softfp_neon-vfpv4
endif

##########################################################################################
#	build busybox
##########################################################################################
busybox: prepare rootfs_prepare
	@echo -e $(GN_FONT)" ########## build busybox ########## "$(DONE)
	$(MAKE) -C $(BUSYBOX_DIR) CHIP=$(CHIP) BOOT_MEDIA=$(BOOT_MEDIA) LLVM=$(LLVM)  LIB_TYPE=$(LIB_TYPE) \
				ARCH_TYPE=$(ARCH_TYPE) BUSYBOX_CFG=$(BUSYBOX_CFG) all

busybox_clean:
	@echo -e $(RD_FONT)" ########## busybox clean ########## "$(DONE)
	$(MAKE)  LLVM=$(LLVM) -C $(BUSYBOX_DIR) distclean

zlib_pc: prepare
	$(MAKE) -C $(ZLIB_DIR) zlib_pc

xz_pc: prepare
	$(MAKE) -C $(XZ_DIR)

mtd_pc: zlib_pc prepare
	$(MAKE) -C $(MTD_DIR) mtd_pc
	cp $(MTD_DIR)/out/pc/mkfs.jffs2 $(OSDRV_DIR)/pub/bin/pc
	cp $(MTD_DIR)/out/pc/mkfs.ubifs $(OSDRV_DIR)/pub/bin/pc
	cp $(MTD_DIR)/out/pc/ubinize $(OSDRV_DIR)/pub/bin/pc

squashfs_pc: zlib_pc xz_pc prepare
	$(MAKE) -C $(SQUASHFS_DIR)
	cp $(SQUASHFS_DIR)/out/pc/mksquashfs $(OSDRV_DIR)/pub/bin/pc

e2fsprogs_pc: prepare
	$(MAKE) -C $(E2FSPROGS_DIR) e2fsprogs_pc
	cp $(E2FSPROGS_DIR)/out/pc/sbin/$(EXT4_TOOL) $(OSDRV_DIR)/pub/bin/pc

nand_production_pc: prepare secure_libs
	$(MAKE) -C $(OSDRV_DIR)/tools/pc/nand_production/fmc_nand_ecc_product_v100
	cp $(OSDRV_DIR)/tools/pc/nand_production/fmc_nand_ecc_product_v100/nand_product $(OSDRV_DIR)/pub/bin/pc

##########################################################################################
#	build pc tools
##########################################################################################
pctools: prepare secure_libs zlib_pc mtd_pc xz_pc squashfs_pc e2fsprogs_pc nand_production_pc
	@echo -e $(GN_FONT)" ########## build tools which run on pc ########## "$(DONE)

pctools_clean:
	@echo -e $(RD_FONT)" ########## build tools which run on pc clean ########## "$(DONE)
	$(MAKE) -C $(ZLIB_DIR) distclean
	$(MAKE) -C $(MTD_DIR) distclean
	$(MAKE) -C $(SQUASHFS_DIR) distclean
	$(MAKE) -C $(XZ_DIR) distclean
	$(MAKE) -C $(E2FSPROGS_DIR)  distclean
	$(MAKE) -C $(OSDRV_DIR)/tools/pc/nand_production/fmc_nand_ecc_product_v100 clean

zlib_board: rootfs_prepare
	$(MAKE) -C $(ZLIB_DIR) LLVM=$(LLVM) OSDRV_CROSS=$(OSDRV_CROSS) zlib_board

eudev_board: rootfs_prepare
	$(MAKE) -C $(EUDEV_DIR) LLVM=$(LLVM) OSDRV_CROSS=$(OSDRV_CROSS)

mtd_board: zlib_board rootfs_prepare
	$(MAKE) -C $(MTD_DIR) LLVM=$(LLVM) OSDRV_CROSS=$(OSDRV_CROSS) mtd_board
	cp $(MTD_DIR)/out/board/bin/* $(OSDRV_DIR)/pub/bin/$(PUB_BOARD)

ethtool_board: rootfs_prepare
	$(MAKE) -C $(ETHTOOL_DIR) LLVM=$(LLVM) OSDRV_CROSS=$(OSDRV_CROSS) all
	cp $(ETHTOOL_DIR)/bin/sbin/ethtool $(OSDRV_DIR)/pub/bin/$(PUB_BOARD) -rf;

e2fsprogs_board: rootfs_prepare
	$(MAKE) -C $(E2FSPROGS_DIR) LLVM=$(LLVM) OSDRV_CROSS=$(OSDRV_CROSS) e2fsprogs_board
	cp $(E2FSPROGS_DIR)/out/board/sbin/mkfs.ext2 $(OSDRV_DIR)/pub/bin/$(PUB_BOARD) -rf;
	cp $(E2FSPROGS_DIR)/out/board/sbin/mkfs.ext3 $(OSDRV_DIR)/pub/bin/$(PUB_BOARD) -rf;
	cp $(E2FSPROGS_DIR)/out/board/sbin/mkfs.ext4 $(OSDRV_DIR)/pub/bin/$(PUB_BOARD) -rf;
	cp $(E2FSPROGS_DIR)/out/board/sbin/mke2fs $(OSDRV_DIR)/pub/bin/$(PUB_BOARD) -rf;

gzip_pc:
ifeq ($(wildcard $(OPEN_SOURCE)/gzip/bin/gzip), )
	$(MAKE) -C $(GZIP_DIR) all
endif
##########################################################################################
#	build board tools
##########################################################################################
boardtools: rootfs_prepare secure_libs
	@echo -e $(GN_FONT)" ########## build tools which run on board ########## "$(DONE)
	$(MAKE) -C $(VENDOR_TOOLS) LLVM=$(LLVM) OSDRV_CROSS=$(OSDRV_CROSS) all

ifeq ($(DEBUG),1)
	cp -af $(VENDOR_TOOLS)/bin/* $(OSDRV_DIR)/pub/$(PUB_ROOTFS)/bin
endif

boardtools_clean:
	$(MAKE) -C $(OSDRV_DIR)/$(VENDOR_TOOLS) clean
	$(MAKE) -C $(EUDEV_DIR) clean
	$(MAKE) -C $(ZLIB_DIR) clean
	$(MAKE) -C $(MTD_DIR) distclean
	$(MAKE) -C $(E2FSPROGS_DIR) distclean
	$(MAKE) -C $(VENDOR_TOOLS) clean
	$(MAKE) -C $(ETHTOOL_DIR) clean
	$(MAKE) -C $(GZIP_DIR)  clean

##########################################################################################
#	build rootfs
##########################################################################################
define rootfs_fun
	@echo -e $(GN_FONT)" ########## build rootfs ########## "$(DONE)
	mkdir -p $(OSDRV_DIR)/pub/$(PUB_ROOTFS)/komod/
	cp -arf $(OUT_DIR)/ko/* $(OSDRV_DIR)/pub/$(PUB_ROOTFS)/komod/
	cp -rf $(SDK_TOP_DIR)/overlay/* $(OSDRV_DIR)/pub/$(PUB_ROOTFS)/
	@echo "compile_time:  "${COMPILE_TIME} >> $(OSDRV_DIR)/pub/$(PUB_ROOTFS)/root/firmware_version
	@echo "commit_id:     "${COMMIT_ID} >> $(OSDRV_DIR)/pub/$(PUB_ROOTFS)/root/firmware_version
	-sed -i 's/^Chip:          .*/Chip:          $(shell echo $(CHIP) | tr a-z A-Z)/' $(OSDRV_DIR)/pub/$(PUB_ROOTFS)/root/firmware_version
	@echo "Author:        "${AUTHOR} >> $(OSDRV_DIR)/pub/$(PUB_ROOTFS)/root/firmware_version
	@chmod 444 $(OSDRV_DIR)/pub/$(PUB_ROOTFS)/root/firmware_version
#	pushd $(YOUYAN_DIR); ./youyan_build.sh $(CHIP)  $(TYPE) ;popd;
	
#	cp -rf $(YOUYAN_DIR)/system $(OSDRV_DIR)/pub/$(PUB_ROOTFS)/	
	rm $(OSDRV_DIR)/pub/$(PUB_ROOTFS)/dev/* -rf
	
	pushd $(OSDRV_DIR)/pub/$(PUB_ROOTFS);ln -s sbin/init init;popd;
	pushd $(OSDRV_DIR)/pub/$(PUB_ROOTFS);chmod go-x * -R;chmod go-w * -R;chmod go-r * -R;popd;
	pushd $(OSDRV_DIR)/pub/$(PUB_ROOTFS);mkdir -p sdcard;popd;
	pushd $(OSDRV_DIR)/pub/$(PUB_ROOTFS);chmod 4755 bin/su;popd;
	pushd $(OSDRV_DIR)/pub/$(PUB_ROOTFS);chmod 4555 bin/login usr/bin/passwd;popd;
	pushd $(OSDRV_DIR)/pub/$(PUB_ROOTFS);chmod 2555 usr/bin/wall;popd;
	pushd $(OSDRV_DIR)/pub/$(PUB_ROOTFS);chmod 1777 tmp;popd;
	pushd $(OSDRV_DIR)/pub/$(PUB_ROOTFS);chmod 755 bin etc lib opt proc usr usr/bin usr/bin/crontab sbin sbin/arp usr/lib var sdcard;popd;
	pushd $(OSDRV_DIR)/pub/$(PUB_ROOTFS);chmod 750 boot etc/init.d etc/init.d/*;popd;
	pushd $(OSDRV_DIR)/pub/$(PUB_ROOTFS);chmod 644 etc/group etc/inittab etc/mtab etc/passwd etc/profile etc/protocols etc/services etc/shadow root/.profile;popd;
	pushd $(OSDRV_DIR)/pub/$(PUB_ROOTFS);chmod 640 var/run/utmp;popd;
	pushd $(OSDRV_DIR)/pub/$(PUB_ROOTFS);chmod 550 bin/mount bin/netstat sbin/route;popd;
	pushd $(OSDRV_DIR)/pub/$(PUB_ROOTFS);$(OSDRV_CROSS)-strip ./lib/*;popd;
	# pushd $(OSDRV_DIR)/pub/bin/pc;fakeroot ./mkfs.jffs2 -d $(OSDRV_DIR)/pub/$(PUB_ROOTFS) -l -e 0x40000 -o $(OSDRV_DIR)/pub/$(PUB_IMAGE)/$(JFFS2_IMAGE_BIN_256K);popd
	# pushd $(OSDRV_DIR)/pub/bin/pc;fakeroot ./mkfs.jffs2 -d $(OSDRV_DIR)/pub/$(PUB_ROOTFS) -l -e 0x20000 -o $(OSDRV_DIR)/pub/$(PUB_IMAGE)/$(JFFS2_IMAGE_BIN_128K);popd
	# pushd $(OSDRV_DIR)/pub/bin/pc;fakeroot ./mkfs.jffs2 -d $(OSDRV_DIR)/pub/$(PUB_ROOTFS) -l -e 0x10000 -o $(OSDRV_DIR)/pub/$(PUB_IMAGE)/$(JFFS2_IMAGE_BIN_64K);popd
	cp $(OSDRV_DIR)/tools/pc/ubi_sh/mkubiimg.sh $(OSDRV_DIR)/pub/$(PUB_IMAGE)
	chmod +x $(OSDRV_DIR)/pub/$(PUB_IMAGE)/mkubiimg.sh
	# build the pagesize = 2k, blocksize = 128k, part_size = 96M #
	pushd $(OSDRV_DIR)/pub/$(PUB_IMAGE);fakeroot ./mkubiimg.sh $(CHIP) 2k 128k $(OSDRV_DIR)/pub/$(PUB_ROOTFS) 96M $(OSDRV_DIR)/pub/bin/pc;popd
	#pushd $(OSDRV_DIR)/pub/$(PUB_IMAGE);fakeroot ./mkubiimg.sh $(CHIP) 2k 128k $(OSDRV_DIR)/pub/$(PUB_ROOTFS) 32M $(OSDRV_DIR)/pub/bin/pc;popd
	# build the pagesize = 4k, blocksize = 256k, part_size = 50M #
	# pushd $(OSDRV_DIR)/pub/$(PUB_IMAGE);fakeroot ./mkubiimg.sh $(CHIP) 4k 256k $(OSDRV_DIR)/pub/$(PUB_ROOTFS) 50M $(OSDRV_DIR)/pub/bin/pc;popd
	rm $(OSDRV_DIR)/pub/$(PUB_IMAGE)/mkubiimg.sh

	# dd if=/dev/zero of=$(OSDRV_DIR)/pub/$(PUB_IMAGE)/$(EXT4_IMAGE_BIN) bs=512 count=$(COUNT_SIZE)
	# $(OSDRV_DIR)/pub/bin/pc/$(EXT4_TOOL) $(OSDRV_DIR)/pub/$(PUB_IMAGE)/$(EXT4_IMAGE_BIN)
	# pushd $(E2FSPROGS_DIR)/out/pc/contrib;fakeroot sh -c "./populate-extfs.sh $(OSDRV_DIR)/pub/$(PUB_ROOTFS) $(OSDRV_DIR)/pub/$(PUB_IMAGE)/$(EXT4_IMAGE_BIN)";popd
	find $(OSDRV_DIR)/pub/$(PUB_ROOTFS)/ -name '*svn' | xargs rm -rf
	pushd $(OSDRV_DIR)/pub;tar czf $(PUB_ROOTFS).tgz $(PUB_ROOTFS);popd
endef
rootfs_build: rootfs_prepare busybox boardtools pctools eudev_board ethtool_board mtd_board zlib_board e2fsprogs_board
	@echo -e $(GREEN)" ########## Build rootfs_build ########## "$(DONE)
	$(call rootfs_fun)
ifeq ($(FASTBOOT_ENABLE),1)
	$(MAKE) rootfs_ramdisk
endif

rootfs_notools_build: 
	@echo -e $(GREEN)" ########## Build rootfs_notools_build ########## "$(DONE)
	$(call rootfs_fun)

ifeq ($(FASTBOOT_ENABLE),1)
	$(MAKE) rootfs_ramdisk
endif

rootfs_clean:
	@echo -e $(RD_FONT)" ########## rootfs clean ########## "$(DONE)
	make -C $(OSDRV_DIR)/rootfs_scripts/ clean
ifeq ($(OSDRV_DIR)/pub/$(PUB_ROOTFS), $(wildcard $(OSDRV_DIR)/pub/$(PUB_ROOTFS)))
	pushd $(OSDRV_DIR)/pub/$(PUB_ROOTFS); chmod +w usr/bin; chmod +w usr/sbin; chmod +w sbin; popd
endif
	rm $(OSDRV_DIR)/pub/$(PUB_ROOTFS)/ -rf

rootfs_ramdisk:
	echo PUB_ROOTFS: $(PUB_ROOTFS)
ifneq ($(wildcard $(OSDRV_DIR)/pub/$(PUB_ROOTFS)),)
	genromfs -d $(OSDRV_DIR)/pub/$(PUB_ROOTFS) -f $(OSDRV_DIR)/pub/$(PUB_ROOTFS).ramdisk
ifeq ($(wildcard $(OPEN_SOURCE)/gzip/bin/gzip), )
	make -C $(OPEN_SOURCE)/gzip all
endif
	cp $(OPEN_SOURCE)/gzip/bin/gzip gzip
	chmod +x gzip
	chmod +x $(OSDRV_DIR)/tools/pc/compress/kernel_compress.sh
	cp $(OSDRV_DIR)/pub/$(PUB_ROOTFS).ramdisk $(OSDRV_DIR)/pub/rootfs.ramdisk.orig
	$(OSDRV_DIR)/tools/pc/compress/kernel_compress.sh $(OSDRV_DIR)/pub/$(PUB_ROOTFS).ramdisk
	cp $(OSDRV_DIR)/pub/$(PUB_ROOTFS).ramdisk.gz $(OSDRV_DIR)/pub/$(PUB_IMAGE)/rootfs_$(CHIP)_ramdisk.gz
	rm gzip
else
	@echo "Not found the ramdisk rootfs folder: $(OSDRV_DIR)/pub/$(PUB_ROOTFS)"
endif

include $(OPTEE_DIR)/Makefile.param
OPTEE_OS_DIR=$(OPTEE_DIR)/$(OPTEE_OS_VER)
OPTEE_CLIENT_DIR=$(OPTEE_DIR)/$(OPTEE_CLIENT_VER)
OPTEE_OS_EXPORT_DIR=$(OPTEE_OS_DIR)/out/arm-plat-vendor

##########################################################################################
#task [10]      build teeos_build
##########################################################################################
teeos:prepare
ifeq ($(TEE_ENABLE), 1)
	@echo -e $(GREEN)"---------task [10.1] build teeos"  $(DONE)
	$(MAKE) -C $(OPTEE_DIR) CHIP=$(CHIP) LIB_TYPE=$(LIB_TYPE) optee_os
endif

teeos_clean:
ifeq ($(TEE_ENABLE), 1)
	$(MAKE) -C $(OPTEE_DIR) optee_os_clean
endif

tee_client: prepare rootfs_prepare
ifeq ($(TEE_ENABLE), 1)
	@echo -e $(GREEN)"---------task [10.2] build tee_client"  $(DONE)
	$(MAKE) -C $(OPTEE_DIR) CHIP=$(CHIP) LIB_TYPE=$(LIB_TYPE) optee_client
	cp -af $(OPTEE_CLIENT_DIR)/out/tee-supplicant/tee-supplicant $(OSDRV_DIR)/pub/$(PUB_ROOTFS)/bin/
	cp -af $(OPTEE_CLIENT_DIR)/out/libckteec/libckteec.so.* $(OSDRV_DIR)/pub/$(PUB_ROOTFS)/lib/
	cp -af $(OPTEE_CLIENT_DIR)/out/libteec/libteec.so.* $(OSDRV_DIR)/pub/$(PUB_ROOTFS)/lib/
endif

tee_client_clean:
ifeq ($(TEE_ENABLE), 1)
	$(MAKE) -C $(OPTEE_DIR) optee_client_clean
endif

##########################################################################################
#	build boot_image.bin
##########################################################################################
gslboot_build:regbin_prepare boot teeos prepare
	@echo -e $(GN_FONT)" ########## build boot_image.bin ########## "$(DONE)
	pushd $(OSDRV_DIR)/components/gsl; make clean; make CHIP=$(CHIP) ;popd

	cp $(OSDRV_DIR)/tools/pc/boot_tools/$(BOOT_REG_BIN) $(BOOT_IMAGE_TOOL_INPUT_DIR)/reg_info.bin;
	cp $(OSDRV_DIR)/components/gsl/pub/gsl.bin $(BOOT_IMAGE_TOOL_INPUT_DIR);
ifeq ($(TEE_ENABLE), 1)
	@echo -e $(GREEN)"---------Secure image"  $(DONE)
	cp $(OPTEE_OS_EXPORT_DIR)/core/tee.bin $(BOOT_IMAGE_TOOL_INPUT_DIR);
endif
	pushd $(BOOT_IMAGE_TOOL_DIR); python $(IMAGE_TOOL_SCRIPT);popd
	cp $(BOOT_IMAGE_TOOL_OUTPUT_FILE) $(OSDRV_DIR)/pub/$(PUB_IMAGE)/boot_image.bin;
ifeq ($(TEE_ENABLE), 1)
	cp $(TEE_IMAGE_TOOL_OUTPUT_FILE) $(OSDRV_DIR)/pub/$(PUB_IMAGE)/tee_image.bin;
endif
	@echo -e $(GREEN)"---------finish osdrv work"  $(DONE)

gslboot_clean:
	pushd $(OSDRV_DIR)/components/gsl; make clean; popd
	rm $(BOOT_IMAGE_TOOL_DIR)/image -rf
	rm $(BOOT_IMAGE_TOOL_DIR)/oem/tmp -rf
	find $(BOOT_IMAGE_TOOL_DIR) -name  '*.pyc'| xargs rm -rf
	find $(BOOT_IMAGE_TOOL_DIR) -name  '__pycache__'| xargs rm -rf
	find $(BOOT_IMAGE_TOOL_INPUT_DIR) -not -path "$(BOOT_IMAGE_TOOL_INPUT_DIR)/cfct_table*.bin" -name "*.bin" | xargs rm -rf

##########################################################################################
#	build u-boot env
##########################################################################################
uboot_env: prepare
	@echo -e $(GN_FONT)" ########## build u-boot env ########## "$(DONE)
ifeq ($(BOOT_MEDIA),emmc)
	make -C $(UBOOT_ENV_DIR) ENVTXT=$(UBOOT_ENV_DIR)/env_text/$(CHIP)/emmc$(TEE_FLAG)_env.txt
	@mv $(UBOOT_ENV_DIR)/env.bin $(OSDRV_DIR)/pub/$(PUB_IMAGE)/emmc$(TEE_FLAG)_env.bin
	@mv $(UBOOT_ENV_DIR)/burn_table.xml $(OSDRV_DIR)/pub/$(PUB_IMAGE)/emmc$(TEE_FLAG)_burn_table.xml
else
	make -C $(UBOOT_ENV_DIR) ENVTXT=$(UBOOT_ENV_DIR)/env_text/$(CHIP)/nand$(TEE_FLAG)_env.txt
	@mv $(UBOOT_ENV_DIR)/env.bin $(OSDRV_DIR)/pub/$(PUB_IMAGE)/nand$(TEE_FLAG)_env.bin
	@mv $(UBOOT_ENV_DIR)/burn_table.xml $(OSDRV_DIR)/pub/$(PUB_IMAGE)/nand$(TEE_FLAG)_burn_table.xml
ifeq ($(FASTBOOT_ENABLE),1)
	make -C $(UBOOT_ENV_DIR) ENVTXT=$(UBOOT_ENV_DIR)/env_text/$(CHIP)/nor$(TEE_FLAG)_quickstart_env.txt SIZE=0x10000
else
	make -C $(UBOOT_ENV_DIR) ENVTXT=$(UBOOT_ENV_DIR)/env_text/$(CHIP)/nor$(TEE_FLAG)_env.txt
endif
	# @mv $(UBOOT_ENV_DIR)/env.bin $(OSDRV_DIR)/pub/$(PUB_IMAGE)/nor$(TEE_FLAG)_env.bin
	# @mv $(UBOOT_ENV_DIR)/burn_table.xml $(OSDRV_DIR)/pub/$(PUB_IMAGE)/nor$(TEE_FLAG)_burn_table.xml
endif
uboot_env_clean:
	@echo -e $(RD_FONT)" ########## uboot_env clean ########## "$(DONE)
	make -C $(OSDRV_DIR)/tools/pc/uboot_env/ clean

##########################################################################################
#	clean pub
##########################################################################################
pub_clean:
	@echo -e $(RD_FONT)" ########## pub clean ########## "$(DONE)
	rm $(OSDRV_DIR)/pub/* -rf

.PHONY: str_firmware str_firmware_clean
