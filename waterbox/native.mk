# native.mk - the native reference: the same SRB2, zlib, libpng and core
# sources as guest.mk, built for the host, plus the harness that drives them.
# Objects land in build/native.
#
# Usage: make -f native.mk -j$(nproc)

.DEFAULT_GOAL := all
include sources.mk

B := $(ROOT)/build/native

SRB2_CFLAGS := $(SRB2_CFLAGS_COMMON) -g -w
ZLIB_CFLAGS := $(ZLIB_CFLAGS_COMMON) -w
PNG_CFLAGS := $(PNG_CFLAGS_COMMON) -w
CORE_CFLAGS := $(CORE_CFLAGS_COMMON) -I. -g -Wall -Wno-unused-function

$(call flags_stamp,$(B),$(SRB2_CFLAGS) | $(ZLIB_CFLAGS) | $(PNG_CFLAGS) | $(CORE_CFLAGS))

SRB2_OBJS := $(patsubst $(SRB2)/%.c,$(B)/srb2/%.o,$(SRB2_SRCS))
ZLIB_OBJS := $(patsubst $(LIBS)/zlib/%.c,$(B)/zlib/%.o,$(ZLIB_SRCS))
PNG_OBJS := $(patsubst $(LIBS)/libpng-src/%.c,$(B)/png/%.o,$(PNG_SRCS))
CORE_OBJS := $(addprefix $(B)/core/,$(addsuffix .o,$(CORE_C_NAMES)))

all: $(B)/run-native

$(B)/srb2/%.o: $(SRB2)/%.c $(PATCH_STAMP) $(PNGCONF) $(B)/flags
	@mkdir -p $(dir $@)
	gcc $(SRB2_CFLAGS) -c -o $@ $<

$(B)/zlib/%.o: $(LIBS)/zlib/%.c $(B)/flags
	@mkdir -p $(dir $@)
	gcc $(ZLIB_CFLAGS) -c -o $@ $<

$(B)/png/%.o: $(LIBS)/libpng-src/%.c $(PNGCONF) $(B)/flags
	@mkdir -p $(dir $@)
	gcc $(PNG_CFLAGS) -c -o $@ $<

$(B)/core/%.o: %.c $(CORE_HDRS) $(PATCH_STAMP) $(PNGCONF) $(B)/flags
	@mkdir -p $(dir $@)
	gcc $(CORE_CFLAGS) -c -o $@ $<

$(B)/core/run-native.o: run-native.c srb2-driver.h platform/chimera-platform.h $(B)/flags
	@mkdir -p $(dir $@)
	gcc -O2 -g -Wall -I. -c -o $@ $<

$(B)/run-native: $(CORE_OBJS) $(SRB2_OBJS) $(PNG_OBJS) $(ZLIB_OBJS) $(B)/core/run-native.o
	gcc -rdynamic -o $@ $^ -lm

clean:
	rm -rf $(B)

.PHONY: all clean
