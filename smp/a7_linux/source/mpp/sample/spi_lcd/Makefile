include ../Makefile.param

FFMPEG_PKG_NAME=ffmpeg-7.1
FFMPEG_PATH=$(OPEN_SOURCE_PATH)/ffmpeg/$(FFMPEG_PKG_NAME)
FFMPEG_OUT=$(PWD)/ffmpeg

########### SRCS & INC ############
INC_FLAGS += -I./
INC_FLAGS += -I$(FFMPEG_OUT)/include
INC_FLAGS += -I./video
INC_FLAGS += -I./mbuffer/include
INC_FLAGS += -I./common
INC_FLAGS += -I$(COMMON_ROOT)/include
ifeq ($(MPP_BUILD), y)
INC_FLAGS += -I$(DRV_ROOT)/$(OT_ARCH)/spi_dma_transfer
else
INC_FLAGS += -I$(DRV_ROOT)/spi_dma_transfer
endif

LIBS_LD_CFLAGS += $(FFMPEG_OUT)/lib/libavcodec.a
LIBS_LD_CFLAGS += $(FFMPEG_OUT)/lib/libavformat.a
LIBS_LD_CFLAGS += $(FFMPEG_OUT)/lib/libavutil.a
LIBS_LD_CFLAGS += $(REL_LIB)/libss_mpi_ive.a

SMP_SRCS := $(wildcard $(PWD)/*.c)
SMP_SRCS += $(wildcard $(PWD)/video/*.c)
SMP_SRCS += $(wildcard $(PWD)/mbuffer/src/*.c)
SMP_SRCS += $(wildcard $(PWD)/common/*.c)

FFMPEG_CONFIGURE=--enable-small \
    --disable-armv5te \
    --disable-armv6 \
    --disable-armv6t2 \
    --disable-linux-perf \
    --enable-static \
    --disable-encoders \
    --disable-muxers \
    --disable-avfilter \
    --enable-swscale \
    --disable-demuxers \
    --enable-demuxer=mov \
    --enable-demuxer=mpegts \
    --disable-protocols \
    --enable-protocol=file \
    --disable-bsfs \
    --enable-bsf=h264_mp4toannexb \
    --enable-bsf=hevc_mp4toannexb \
    --disable-parsers \
    --enable-parser=hevc \
    --enable-parser=h264 \
    --disable-iconv \
    --disable-avdevice \
    --disable-network \
    --disable-decoders \
    --enable-decoder=h264 \
    --enable-decoder=hevc \
    --enable-decoder=mjpeg \
    --enable-asm \
    --disable-filters \
    --enable-inline-asm \
    --enable-neon \
    --disable-shared \
    --disable-debug \
    --disable-doc \
    --disable-swresample \
    --disable-programs \
    --disable-symver \
    --target-os=linux \
    --enable-cross-compile \
    --disable-linux-perf \
    --disable-mediafoundation \
    --disable-large-tests \
    --extra-cflags="-fPIC -mlong-calls -Wno-switch-unreachable -Wno-format-truncation -Wno-memset-elt-size" \
    --arch=arm \
    --cpu=cortex-a7 \
    --prefix=$(FFMPEG_OUT) \
    --cross-prefix=$(CONFIG_OT_CROSS_LIB)

TARGET := sample_spi_lcd

TARGET_PATH := $(PWD)

CFLAGS += $(INC_FLAGS)
CFLAGS += $(COMM_INC)

OBJS  := $(SMP_SRCS:%.c=%.o)
MPI_LIBS += $(LIBS_LD_CFLAGS)

ifeq ($(wildcard $(FFMPEG_PATH).tar.gz),)
$(warn "$(FFMPEG_PATH).tar.gz not exist, please download it first")
endif

.PHONY : all clean ffmpeg ffmpeg_clean prepare

ifeq ($(wildcard $(FFMPEG_PATH).tar.gz),)
all:
clean:
else
# compile linux or LITEOS
all: $(TARGET)

ffmpeg:
ifeq ($(wildcard $(FFMPEG_OUT)/lib),)
ifeq ($(wildcard $(FFMPEG_PATH)),)
	@cd $(OPEN_SOURCE_PATH)/ffmpeg && tar xf $(FFMPEG_PKG_NAME).tar.gz
endif
	@cd $(FFMPEG_PATH) && ./configure $(FFMPEG_CONFIGURE)
	@cd $(FFMPEG_PATH) && make install
endif

ffmpeg_clean:
ifneq ($(wildcard $(FFMPEG_PATH)),)
	@cd $(FFMPEG_PATH) && make clean
endif

clean: ffmpeg_clean
	@rm -f $(TARGET_PATH)/$(TARGET)
	@rm -f $(OBJS)
	@rm -f $(COMM_OBJ)

$(OBJS): ffmpeg

$(TARGET): $(COMM_OBJ) $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET_PATH)/$@ $^ -Wl,--start-group $(MPI_LIBS) $(SENSOR_LIBS) $(AUDIO_LIBA) $(REL_LIB)/libsecurec.a -lm -lc -Wl,--end-group
endif
