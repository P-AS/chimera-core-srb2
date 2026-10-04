# guest.mk - core.wbx: the same SRB2, zlib, libpng and core sources as
# native.mk, built with miniBox's musl guest toolchain and linked at the guest
# base. Objects land in build/guest; core.wbx is checked by miniBox's
# check-wbx.sh (no thread-local storage, no %fs, no red zone) before it counts
# as built. SRB2 is C: the plain guest toolchain, no libstdc++.
#
# Needs miniBox built: meson setup <miniBox>/build/meson-linux <miniBox> && ninja -C <miniBox>/build/meson-linux
#
# Usage: make -f guest.mk -j$(nproc) MINIBOX_DIR=<miniBox checkout>

.DEFAULT_GOAL := all
include sources.mk

ifeq ($(MB),)
$(error name miniBox: make -f guest.mk MINIBOX_DIR=<miniBox checkout>)
endif

B      := $(ROOT)/build/guest
MBUILD := $(MB)/build/meson-linux
CC     := $(MBUILD)/musl-gcc
EMULIBC := $(MBUILD)/source/guest/emulibc.c.o

# BizHawk waterbox's frozen guest flags, as miniBox's source/guest/meson.build
# gives them to a guest
WBFLAGS := -fvisibility=hidden -mcmodel=large -mno-red-zone -mstack-protector-guard=global \
	-fno-stack-protector -fno-pic -fno-pie -fcf-protection=none -DNDEBUG -DCHIMERA_GUEST
MBINCS := -I$(MB)/extern/emulibc -I$(MB)/source/guest/include -I$(MB)/extern/jsmn

SRB2_CFLAGS := $(WBFLAGS) $(SRB2_CFLAGS_COMMON) -w
ZLIB_CFLAGS := $(WBFLAGS) $(ZLIB_CFLAGS_COMMON) -w
PNG_CFLAGS := $(WBFLAGS) $(PNG_CFLAGS_COMMON) -w
CORE_CFLAGS := $(WBFLAGS) $(CORE_CFLAGS_COMMON) $(MBINCS) -I. -Wall -Wno-unused-function

$(call flags_stamp,$(B),$(CC) | $(SRB2_CFLAGS) | $(ZLIB_CFLAGS) | $(PNG_CFLAGS) | $(CORE_CFLAGS))

SRB2_OBJS := $(patsubst $(SRB2)/%.c,$(B)/srb2/%.o,$(SRB2_SRCS))
ZLIB_OBJS := $(patsubst $(LIBS)/zlib/%.c,$(B)/zlib/%.o,$(ZLIB_SRCS))
PNG_OBJS := $(patsubst $(LIBS)/libpng-src/%.c,$(B)/png/%.o,$(PNG_SRCS))
CORE_OBJS := $(addprefix $(B)/core/,$(addsuffix .o,$(CORE_C_NAMES))) $(B)/core/libco.o

all: $(B)/core.wbx

$(CC) $(EMULIBC):
	@echo "miniBox's guest toolchain is missing: $@" >&2
	@echo "build it: meson setup $(MBUILD) $(MB) && ninja -C $(MBUILD)" >&2
	@false

$(B)/srb2/%.o: $(SRB2)/%.c $(PATCH_STAMP) $(PNGCONF) $(B)/flags | $(CC)
	@mkdir -p $(dir $@)
	$(CC) $(SRB2_CFLAGS) -c -o $@ $<

$(B)/zlib/%.o: $(LIBS)/zlib/%.c $(B)/flags | $(CC)
	@mkdir -p $(dir $@)
	$(CC) $(ZLIB_CFLAGS) -c -o $@ $<

$(B)/png/%.o: $(LIBS)/libpng-src/%.c $(PNGCONF) $(B)/flags | $(CC)
	@mkdir -p $(dir $@)
	$(CC) $(PNG_CFLAGS) -c -o $@ $<

$(B)/core/%.o: %.c $(CORE_HDRS) $(PATCH_STAMP) $(PNGCONF) $(B)/flags | $(CC)
	@mkdir -p $(dir $@)
	$(CC) $(CORE_CFLAGS) -c -o $@ $<

# miniBox's C guest link: static, no PIE, at the guest base (linkscript.T)
$(B)/core.wbx: $(CORE_OBJS) $(SRB2_OBJS) $(PNG_OBJS) $(ZLIB_OBJS) | $(EMULIBC)
	$(CC) -static -no-pie -Wl,--eh-frame-hdr,-O2 -Wl,-z,stack-size=8388608 -T $(MB)/source/guest/linkscript.T \
		-o $@.tmp $^ $(EMULIBC) $(WRAP_FLAGS) -lm -lgcc
	sh $(MB)/source/guest/check-wbx.sh $@.tmp
	mv $@.tmp $@

$(B)/core/libco.o: $(LIBCO_SRC) $(B)/flags
	@mkdir -p $(dir $@)
	$(CC) $(CORE_CFLAGS) -std=gnu11 -c -o $@ $<

clean:
	rm -rf $(B)

.PHONY: all clean
