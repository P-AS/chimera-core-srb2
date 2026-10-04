# native.mk - the native reference: the same SRB2, zlib, libpng and core
# sources as guest.mk, built for the host, plus the two harnesses (run-native
# drives the exports directly; run-wbx drives core.wbx through the miniBox host
# exactly as the frontend does). Objects land in build/native.
#
# Usage: make -f native.mk -j$(nproc) [MINIBOX_DIR=<miniBox checkout>]
#   miniBox's guest kit headers (the settings reader) are compiled into both
#   builds; run-wbx needs its host library built (meson setup build/meson-linux).

.DEFAULT_GOAL := all
include sources.mk

B := $(ROOT)/build/native

SRB2_CFLAGS := $(SRB2_CFLAGS_COMMON) -g -w
ZLIB_CFLAGS := $(ZLIB_CFLAGS_COMMON) -w
PNG_CFLAGS := $(PNG_CFLAGS_COMMON) -w
CORE_CFLAGS := $(CORE_CFLAGS_COMMON) -Inative-shim -I$(MB)/source/guest/include -I$(MB)/extern/jsmn -I. -g -Wall -Wno-unused-function

$(call flags_stamp,$(B),$(SRB2_CFLAGS) | $(ZLIB_CFLAGS) | $(PNG_CFLAGS) | $(CORE_CFLAGS))

SRB2_OBJS := $(patsubst $(SRB2)/%.c,$(B)/srb2/%.o,$(SRB2_SRCS))
ZLIB_OBJS := $(patsubst $(LIBS)/zlib/%.c,$(B)/zlib/%.o,$(ZLIB_SRCS))
PNG_OBJS := $(patsubst $(LIBS)/libpng-src/%.c,$(B)/png/%.o,$(PNG_SRCS))
CORE_OBJS := $(addprefix $(B)/core/,$(addsuffix .o,$(CORE_C_NAMES))) $(B)/core/libco.o

ifeq ($(MB),)
$(error name miniBox: make -f native.mk MINIBOX_DIR=<miniBox checkout>)
endif

all: $(B)/run-native $(B)/run-wbx

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

$(B)/core/run-native.o: run-native.c harness.h platform/chimera-platform.h $(B)/flags
	@mkdir -p $(dir $@)
	gcc -O2 -g -Wall -I. -c -o $@ $<

$(B)/run-native: $(CORE_OBJS) $(SRB2_OBJS) $(PNG_OBJS) $(ZLIB_OBJS) $(B)/core/run-native.o
	gcc -rdynamic -o $@ $^ $(WRAP_FLAGS) -lm

# run-wbx links the miniBox host library
MBHOST := $(MB)/build/meson-linux/source/host
$(B)/run-wbx: run-wbx.c harness.h $(B)/flags
	@test -f $(MBHOST)/libminiboxhost.so || { echo "miniBox's host is not built: meson setup $(MB)/build/meson-linux $(MB) && ninja -C $(MB)/build/meson-linux" >&2; false; }
	gcc -O2 -g -Wall -I. -I$(MB)/source/host -o $@ run-wbx.c $(MBHOST)/libminiboxhost.so -Wl,-rpath,$(MBHOST)

$(B)/core/libco.o: $(LIBCO_SRC) $(B)/flags
	@mkdir -p $(dir $@)
	gcc $(CORE_CFLAGS) -std=gnu11 -c -o $@ $<

clean:
	rm -rf $(B)

.PHONY: all clean
