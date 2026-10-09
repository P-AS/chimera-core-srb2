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
# gives them to a guest. The guest is built for the CPU it runs on: x86-64, or
# miniBox's aarch64 machine (no return-address signing, no outline atomics, and
# x86-64's arithmetic: no fused multiply-add, signed char). The musl specs
# carry the CPU flags for C only, so they are given here for C++ as well.
#
# And on aarch64, x18 left alone (-ffixed-x18). A package is machine code for
# a CPU, not an OS, and x18 is the one register the OSes disagree on: Linux
# lets code use it, Windows keeps the thread's TEB in it (its ARM64 ABI:
# "reserved platform register"), and macOS zeroes it when it likes. A core
# that uses it could only ever run on Linux, and the package a movie cites
# cannot be rebuilt later without becoming a different package - so it is
# kept out now, while there are no aarch64 nightlies to keep.
# check-portable.sh holds every object built here to it.
GUEST_CPU := $(shell uname -m)
ifeq ($(GUEST_CPU),aarch64)
WBCPU := -mbranch-protection=none -mno-outline-atomics -ffp-contract=off -fsigned-char -ffixed-x18
GUEST_TRIPLET := aarch64-linux-musl
else ifeq ($(GUEST_CPU),x86_64)
WBCPU := -mcmodel=large -mno-red-zone -fcf-protection=none
GUEST_TRIPLET := x86_64-linux-musl
else
$(error miniBox runs on x86-64 and aarch64 only, not $(GUEST_CPU))
endif
WBFLAGS := -fvisibility=hidden $(WBCPU) -mstack-protector-guard=global \
	-fno-stack-protector -fno-pic -fno-pie -DNDEBUG -DCHIMERA_GUEST
MBINCS := -I$(MB)/extern/emulibc -I$(MB)/source/guest/include -I$(MB)/extern/jsmn
CXXINCS := -I$(SR)/include/c++/$(GCCVER) -I$(SR)/include/c++/$(GCCVER)/$(GUEST_TRIPLET)

SRB2_CFLAGS := $(WBFLAGS) $(SRB2_CFLAGS_COMMON) -w $(SRB2_RENAMES)
ZLIB_CFLAGS := $(WBFLAGS) $(ZLIB_CFLAGS_COMMON) -w
PNG_CFLAGS := $(WBFLAGS) $(PNG_CFLAGS_COMMON) -w
XIPH_CFLAGS := $(WBFLAGS) $(XIPH_CFLAGS_COMMON) -w
GME_CXXFLAGS := $(WBFLAGS) $(GME_CXXFLAGS_COMMON) $(CXXINCS) -w
GME_CFLAGS := $(WBFLAGS) $(GME_CFLAGS_COMMON) -w
OPENMPT_CXXFLAGS := $(WBFLAGS) $(OPENMPT_CXXFLAGS_COMMON) $(CXXINCS) -w
CORE_CFLAGS := $(WBFLAGS) $(CORE_CFLAGS_COMMON) $(MBINCS) -I. -Wall -Wno-unused-function

$(call flags_stamp,$(B),$(CC) | $(CXX) | $(SRB2_CFLAGS) | $(ZLIB_CFLAGS) | $(PNG_CFLAGS) | $(XIPH_CFLAGS) | $(GME_CXXFLAGS) | $(GME_CFLAGS) | $(OPENMPT_CXXFLAGS) | $(CORE_CFLAGS))

SRB2_OBJS := $(patsubst $(SRB2)/%.c,$(B)/srb2/%.o,$(SRB2_SRCS))
ZLIB_OBJS := $(patsubst $(LIBS)/zlib/%.c,$(B)/zlib/%.o,$(ZLIB_SRCS))
PNG_OBJS := $(patsubst $(LIBS)/libpng-src/%.c,$(B)/png/%.o,$(PNG_SRCS))
XIPH_OBJS := $(patsubst $(OGG)/src/%.c,$(B)/ogg/%.o,$(OGG_SRCS)) $(patsubst $(VORBIS)/lib/%.c,$(B)/vorbis/%.o,$(VORBIS_SRCS))
OPENMPT_OBJS := $(patsubst $(OPENMPT)/%.cpp,$(B)/openmpt/%.o,$(OPENMPT_SRCS))
GME_OBJS := $(patsubst $(GME)/%.cpp,$(B)/gme/%.o,$(filter %.cpp,$(GME_SRCS))) $(patsubst $(GME)/%.c,$(B)/gme/%.o,$(filter %.c,$(GME_SRCS)))
CORE_OBJS := $(addprefix $(B)/core/,$(addsuffix .o,$(CORE_C_NAMES))) $(B)/core/libco.o

all: $(B)/core.wbx

$(LIBSTDCXX) $(EMULIBC) $(CXXGLUE):
	@echo "miniBox's C++ guest toolchain is missing: $@" >&2
	@echo "build it: meson setup $(MBUILD) $(MB) -Dguest_cpp=true &&" >&2
	@echo "  ninja -C $(MBUILD) libstdcxx-installed.stamp source/guest/emulibc.c.o source/guest/cxxglue.c.o" >&2
	@false

$(B)/srb2/%.o: $(SRB2)/%.c $(PATCH_STAMP) $(OPENMPT_STAMP) $(PNGCONF) $(B)/flags | $(LIBSTDCXX) $(MESA_GL_H)
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

$(B)/openmpt/%.o: $(OPENMPT)/%.cpp $(OPENMPT_STAMP) $(B)/flags | $(LIBSTDCXX)
	@mkdir -p $(dir $@)
	$(CXX) $(OPENMPT_CXXFLAGS) -c -o $@ $<

$(B)/gme/%.o: $(GME)/%.cpp $(B)/flags | $(LIBSTDCXX)
	@mkdir -p $(dir $@)
	$(CXX) $(GME_CXXFLAGS) -c -o $@ $<

$(B)/gme/%.o: $(GME)/%.c $(B)/flags | $(LIBSTDCXX)
	@mkdir -p $(dir $@)
	$(CC) $(GME_CFLAGS) -c -o $@ $<

$(B)/core/%.o: %.c $(CORE_HDRS) $(PATCH_STAMP) $(OPENMPT_STAMP) $(PNGCONF) $(B)/flags | $(LIBSTDCXX) $(MESA_GL_H)
	@mkdir -p $(dir $@)
	$(CC) $(GL_INCS) $(CORE_CFLAGS) -c -o $@ $<

# The GPU bridge's guest half (Chimera's docs/gpu-bridge.md): a wrapper per
# entry point this core calls (gl-bridge-list.txt), generated by miniBox's
# source/gl/gen-gl-bridge.py from glad's declarations (waterbox/glad, the copy
# Chimera's engine vendors) with the master list's opcodes; glad's gl.c holds
# the pointers it fills. gl_compat.c is what calls them.
GL_INCS := -Iglad/include -I$(MB)/source/gl -I$(B)/glbridge
GLGEN := $(MB)/source/gl/gen-gl-bridge.py
GLMASTER := $(MB)/source/gl/gl-entry-points.txt
$(B)/glbridge/gl-bridge-guest.cpp: gl-bridge-list.txt $(GLGEN) $(GLMASTER) glad/include/glad/gl.h
	@mkdir -p $(dir $@)
	python3 $(GLGEN) glad/include/glad/gl.h $(GLMASTER) $(B)/glbridge --only gl-bridge-list.txt

$(B)/glbridge/gl-bridge-guest.o: $(B)/glbridge/gl-bridge-guest.cpp $(B)/flags | $(LIBSTDCXX)
	$(CXX) $(WBFLAGS) -O2 $(CXXINCS) $(GL_INCS) -w -c -o $@ $<

$(B)/glbridge/glad.o: glad/src/gl.c $(B)/flags | $(LIBSTDCXX)
	@mkdir -p $(dir $@)
	$(CC) $(WBFLAGS) -O2 -Iglad/include -w -c -o $@ $<

$(B)/core/platform/gl_compat.o: $(B)/glbridge/gl-bridge-guest.cpp

$(B)/core/libco.o: $(LIBCO_SRC) $(B)/flags | $(LIBSTDCXX)
	@mkdir -p $(dir $@)
	$(CC) $(CORE_CFLAGS) -std=gnu11 -c -o $@ $<

# Mesa, for the guest (waterbox/setup-mesa.sh): its static archives, linked
# in a group because they refer to each other both ways, and the osmesa
# target's own object, which holds osmesa_create_screen (the shared library it
# belongs to cannot be built for a guest). Mesa declares pthread_mutexattr_*
# weak; statically linked they would resolve to address zero, so they are
# pulled in by name. As chimera-core-flycast links it.
MESA_BUILD := $(MESA)/build-guest2
MESA_TARGET := $(MESA_BUILD)/src/gallium/targets/osmesa/libOSMesa.so.8.0.0.p/target.c.o
MESA_ARCHIVES = $(shell find $(MESA_BUILD) -name '*.a' 2>/dev/null | sort)
MESA_LINK = $(MESA_TARGET) -Wl,--start-group $(MESA_ARCHIVES) -Wl,--end-group \
	-Wl,-u,pthread_mutexattr_init -Wl,-u,pthread_mutexattr_settype -Wl,-u,pthread_mutexattr_destroy

$(MESA_TARGET):
	@echo "no guest Mesa at $(MESA_BUILD): run waterbox/setup-mesa.sh first" >&2
	@false

# miniBox's C++ guest link recipe (source/guest/meson.build): the large code
# model's --no-relax, the weak pthread references libgcc_eh pulls, cxxglue
# (__dso_handle, _dl_find_object), and the libraries in this order, libc last
$(B)/core.wbx: $(CORE_OBJS) $(SRB2_OBJS) $(PNG_OBJS) $(XIPH_OBJS) $(GME_OBJS) $(OPENMPT_OBJS) $(ZLIB_OBJS) $(B)/glbridge/gl-bridge-guest.o $(B)/glbridge/glad.o $(MESA_TARGET) | $(EMULIBC) $(CXXGLUE)
	$(CXX) -static -no-pie -Wl,--eh-frame-hdr,-O2,--no-relax -Wl,-z,stack-size=8388608 \
		-T $(MB)/source/guest/linkscript.T \
		-Wl,-u,pthread_once -Wl,-u,pthread_cond_wait -Wl,-u,pthread_cond_broadcast -Wl,-u,pthread_key_create \
		-o $@.tmp $(filter-out $(MESA_TARGET),$^) $(MESA_LINK) $(CXXGLUE) $(EMULIBC) $(WRAP_FLAGS) -L$(SR)/lib -lstdc++ -lm -lgcc -lgcc_eh -lc
	sh $(MB)/source/guest/check-wbx.sh $@.tmp
	sh check-portable.sh $@.tmp $(filter-out $(MESA_TARGET),$^) $(MESA_TARGET) $(MESA_ARCHIVES)
	mv $@.tmp $@

clean:
	rm -rf $(B)

.PHONY: all clean
