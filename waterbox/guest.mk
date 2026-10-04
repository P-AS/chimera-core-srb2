# guest.mk - core.wbx: the same SRB2, zlib, libpng, libogg, libvorbis, GME and
# core sources as native.mk, built with miniBox's musl/libstdc++ guest
# toolchain (GME is C++) and linked at the guest base. Objects land in
# build/guest; core.wbx is checked by miniBox's check-wbx.sh (no thread-local
# storage, no %fs, no red zone) before it counts as built.
#
# Needs miniBox built WITH the C++ guest toolchain:
#   meson setup <miniBox>/build/meson-cpp <miniBox> -Dguest_cpp=true
#   ninja -C <miniBox>/build/meson-cpp libstdcxx-installed.stamp source/guest/emulibc.c.o source/guest/cxxglue.c.o
#
# Usage: make -f guest.mk -j$(nproc) MINIBOX_DIR=<miniBox checkout>

.DEFAULT_GOAL := all
include sources.mk

ifeq ($(MB),)
$(error name miniBox: make -f guest.mk MINIBOX_DIR=<miniBox checkout>)
endif

B      := $(ROOT)/build/guest
MBUILD := $(MB)/build/meson-cpp
SR     := $(MBUILD)/guest-sysroot
GCCVER := $(shell gcc -dumpfullversion)
# the system's compilers over the guest sysroot, through its specs (musl's
# headers and start files), as miniBox's C++ guests and the DSDA core build
CC     := gcc -specs $(SR)/lib/musl-gcc.specs
CXX    := g++ -specs $(SR)/lib/musl-gcc.specs
EMULIBC := $(MBUILD)/source/guest/emulibc.c.o
CXXGLUE := $(MBUILD)/source/guest/cxxglue.c.o
LIBSTDCXX := $(SR)/lib/libstdc++.a

# BizHawk waterbox's frozen guest flags, as miniBox's source/guest/meson.build
# gives them to a guest
WBFLAGS := -fvisibility=hidden -mcmodel=large -mno-red-zone -mstack-protector-guard=global \
	-fno-stack-protector -fno-pic -fno-pie -fcf-protection=none -DNDEBUG -DCHIMERA_GUEST
MBINCS := -I$(MB)/extern/emulibc -I$(MB)/source/guest/include -I$(MB)/extern/jsmn
CXXINCS := -I$(SR)/include/c++/$(GCCVER) -I$(SR)/include/c++/$(GCCVER)/x86_64-linux-musl

SRB2_CFLAGS := $(WBFLAGS) $(SRB2_CFLAGS_COMMON) -w
ZLIB_CFLAGS := $(WBFLAGS) $(ZLIB_CFLAGS_COMMON) -w
PNG_CFLAGS := $(WBFLAGS) $(PNG_CFLAGS_COMMON) -w
XIPH_CFLAGS := $(WBFLAGS) $(XIPH_CFLAGS_COMMON) -w
GME_CXXFLAGS := $(WBFLAGS) $(GME_CXXFLAGS_COMMON) $(CXXINCS) -w
GME_CFLAGS := $(WBFLAGS) $(GME_CFLAGS_COMMON) -w
CORE_CFLAGS := $(WBFLAGS) $(CORE_CFLAGS_COMMON) $(MBINCS) -I. -Wall -Wno-unused-function

$(call flags_stamp,$(B),$(CC) | $(CXX) | $(SRB2_CFLAGS) | $(ZLIB_CFLAGS) | $(PNG_CFLAGS) | $(XIPH_CFLAGS) | $(GME_CXXFLAGS) | $(GME_CFLAGS) | $(CORE_CFLAGS))

SRB2_OBJS := $(patsubst $(SRB2)/%.c,$(B)/srb2/%.o,$(SRB2_SRCS))
ZLIB_OBJS := $(patsubst $(LIBS)/zlib/%.c,$(B)/zlib/%.o,$(ZLIB_SRCS))
PNG_OBJS := $(patsubst $(LIBS)/libpng-src/%.c,$(B)/png/%.o,$(PNG_SRCS))
XIPH_OBJS := $(patsubst $(OGG)/src/%.c,$(B)/ogg/%.o,$(OGG_SRCS)) $(patsubst $(VORBIS)/lib/%.c,$(B)/vorbis/%.o,$(VORBIS_SRCS))
GME_OBJS := $(patsubst $(GME)/%.cpp,$(B)/gme/%.o,$(filter %.cpp,$(GME_SRCS))) $(patsubst $(GME)/%.c,$(B)/gme/%.o,$(filter %.c,$(GME_SRCS)))
CORE_OBJS := $(addprefix $(B)/core/,$(addsuffix .o,$(CORE_C_NAMES))) $(B)/core/libco.o

all: $(B)/core.wbx

$(LIBSTDCXX) $(EMULIBC) $(CXXGLUE):
	@echo "miniBox's C++ guest toolchain is missing: $@" >&2
	@echo "build it: meson setup $(MBUILD) $(MB) -Dguest_cpp=true &&" >&2
	@echo "  ninja -C $(MBUILD) libstdcxx-installed.stamp source/guest/emulibc.c.o source/guest/cxxglue.c.o" >&2
	@false

$(B)/srb2/%.o: $(SRB2)/%.c $(PATCH_STAMP) $(PNGCONF) $(B)/flags | $(LIBSTDCXX)
	@mkdir -p $(dir $@)
	$(CC) $(SRB2_CFLAGS) -c -o $@ $<

$(B)/zlib/%.o: $(LIBS)/zlib/%.c $(B)/flags | $(LIBSTDCXX)
	@mkdir -p $(dir $@)
	$(CC) $(ZLIB_CFLAGS) -c -o $@ $<

$(B)/png/%.o: $(LIBS)/libpng-src/%.c $(PNGCONF) $(B)/flags | $(LIBSTDCXX)
	@mkdir -p $(dir $@)
	$(CC) $(PNG_CFLAGS) -c -o $@ $<

$(B)/ogg/%.o: $(OGG)/src/%.c compat/ogg/config_types.h $(B)/flags | $(LIBSTDCXX)
	@mkdir -p $(dir $@)
	$(CC) $(XIPH_CFLAGS) -c -o $@ $<

$(B)/vorbis/%.o: $(VORBIS)/lib/%.c compat/ogg/config_types.h $(B)/flags | $(LIBSTDCXX)
	@mkdir -p $(dir $@)
	$(CC) $(XIPH_CFLAGS) -c -o $@ $<

$(B)/gme/%.o: $(GME)/%.cpp $(B)/flags | $(LIBSTDCXX)
	@mkdir -p $(dir $@)
	$(CXX) $(GME_CXXFLAGS) -c -o $@ $<

$(B)/gme/%.o: $(GME)/%.c $(B)/flags | $(LIBSTDCXX)
	@mkdir -p $(dir $@)
	$(CC) $(GME_CFLAGS) -c -o $@ $<

$(B)/core/%.o: %.c $(CORE_HDRS) $(PATCH_STAMP) $(PNGCONF) $(B)/flags | $(LIBSTDCXX)
	@mkdir -p $(dir $@)
	$(CC) $(CORE_CFLAGS) -c -o $@ $<

$(B)/core/libco.o: $(LIBCO_SRC) $(B)/flags | $(LIBSTDCXX)
	@mkdir -p $(dir $@)
	$(CC) $(CORE_CFLAGS) -std=gnu11 -c -o $@ $<

# miniBox's C++ guest link recipe (source/guest/meson.build): the large code
# model's --no-relax, the weak pthread references libgcc_eh pulls, cxxglue
# (__dso_handle, _dl_find_object), and the libraries in this order, libc last
$(B)/core.wbx: $(CORE_OBJS) $(SRB2_OBJS) $(PNG_OBJS) $(XIPH_OBJS) $(GME_OBJS) $(ZLIB_OBJS) | $(EMULIBC) $(CXXGLUE)
	$(CXX) -static -no-pie -Wl,--eh-frame-hdr,-O2,--no-relax -Wl,-z,stack-size=8388608 \
		-T $(MB)/source/guest/linkscript.T \
		-Wl,-u,pthread_once -Wl,-u,pthread_cond_wait -Wl,-u,pthread_cond_broadcast -Wl,-u,pthread_key_create \
		-o $@.tmp $^ $(CXXGLUE) $(EMULIBC) $(WRAP_FLAGS) -L$(SR)/lib -lstdc++ -lm -lgcc -lgcc_eh -lc
	sh $(MB)/source/guest/check-wbx.sh $@.tmp
	mv $@.tmp $@

clean:
	rm -rf $(B)

.PHONY: all clean
