# sources.mk - what native.mk and guest.mk both build: the same files with the
# same defines, so the native reference and the sandboxed core are the same
# program. Included, not run.

ROOT := ..
SRB2 := $(ROOT)/extern/SRB2/src
LIBS := $(ROOT)/extern/SRB2/libs
MB   ?= $(MINIBOX_DIR)
# libpng's configuration header: its prebuilt one, copied beside the build
PNGCONF_DIR := $(ROOT)/build/pngconf
PNGCONF := $(PNGCONF_DIR)/pnglibconf.h

# ---- SRB2, upstream (the submodule extern/SRB2): its engine - the game, the
# software renderer, Lua, the console and menus, the netcode a local game runs
# through - less its platform layer (sdl/), which the core is instead
# (platform/). The lists are upstream's own Sourcefiles, so an upstream bump
# brings its new files without an edit here. Left out:
#   comptime.c           the build's date and git state: platform/comptime.c
#                        names the core's build instead, the same in both
#   netcode/i_tcp.c      sockets: the machine has no network (dummy/i_net.c)
#   hardware/            the OpenGL renderer: the core draws in software
#   sdl/, dedicated/     upstream's platform layers
# Taken from upstream's dummy/ interface as it is: i_sound.c (silence, until
# the core mixes its own), i_net.c (no network), i_cdmus.c; and from sdl/,
# dosstr.c (strupr, strlwr). md5.c and apng.c are upstream's optional files,
# in as its Linux build has them.
srb2_list = $(addprefix $(SRB2)/$(1),$(filter %.c,$(shell cat $(SRB2)/$(1)Sourcefile)))
SRB2_EXCLUDE := $(SRB2)/comptime.c $(SRB2)/netcode/i_tcp.c
SRB2_SRCS := $(filter-out $(SRB2_EXCLUDE), \
	$(call srb2_list,) $(call srb2_list,blua/) $(call srb2_list,netcode/) \
	$(SRB2)/md5.c $(SRB2)/apng.c $(SRB2)/sdl/dosstr.c $(SRB2)/dummy/i_sound.c $(SRB2)/dummy/i_net.c $(SRB2)/dummy/i_cdmus.c)
# upstream's defines (src/CMakeLists.txt, a Linux x86-64 build): software
# renderer only (no HWRENDER), no threads, no curl, no UPnP, no Mumble, no
# music libraries; zlib (the .pk3 files) and libpng (PNG graphics in them)
# from upstream's own libs/. C23, with -fwrapv: the game relies on wrapping
# signed arithmetic. NDEBUG, as upstream's release build and the guest have it.
SRB2_DEFS := -DNDEBUG -DUNIXCOMMON -DLINUX -DLINUX64 -D_LARGEFILE64_SOURCE -DHAVE_ZLIB -DHAVE_PNG \
	-DNOMUMBLE -DNOEXECINFO -DNOUPNP
SRB2_INCS := -Iplatform -I$(SRB2) -I$(SRB2)/blua -I$(LIBS)/zlib -I$(LIBS)/libpng-src -I$(PNGCONF_DIR)
SRB2_CFLAGS_COMMON := -std=gnu23 -O2 -fwrapv -fno-strict-aliasing $(SRB2_DEFS) $(SRB2_INCS)

# ---- zlib (extern/SRB2/libs/zlib): the .pk3 files' deflate, and libpng's
ZLIB_NAMES := adler32 compress crc32 deflate inffast inflate inftrees trees uncompr zutil
ZLIB_SRCS := $(addprefix $(LIBS)/zlib/,$(addsuffix .c,$(ZLIB_NAMES)))
ZLIB_CFLAGS_COMMON := -std=gnu11 -O2 -DNDEBUG -I$(LIBS)/zlib

# ---- libpng (extern/SRB2/libs/libpng-src): its own prebuilt configuration,
# no SIMD filters (plain C, the same everywhere)
PNG_NAMES := png pngerror pngget pngmem pngpread pngread pngrio pngrtran pngrutil pngset pngtrans \
	pngwio pngwrite pngwtran pngwutil
PNG_SRCS := $(addprefix $(LIBS)/libpng-src/,$(addsuffix .c,$(PNG_NAMES)))
PNG_CFLAGS_COMMON := -std=gnu11 -O2 -DNDEBUG -DPNG_INTEL_SSE_OPT=0 -DPNG_ARM_NEON_OPT=0 \
	-DPNG_MIPS_MSA_OPT=0 -DPNG_POWERPC_VSX_OPT=0 -I$(LIBS)/libpng-src -I$(PNGCONF_DIR) -I$(LIBS)/zlib

# ---- the core: its platform layer (platform/) and the driver
PLATFORM_NAMES := i_system i_video i_threads i_net files comptime
CORE_C_NAMES := $(addprefix platform/,$(PLATFORM_NAMES)) srb2-driver wbx-entry
# libco (miniBox's extern/libco, public domain): the engine's cothread
LIBCO_SRC := $(MB)/extern/libco/amd64.c
CORE_HDRS := $(wildcard *.h) $(wildcard platform/*.h)
CORE_CFLAGS_COMMON := $(SRB2_CFLAGS_COMMON) -I$(MB)/extern/libco

# the calls the core answers itself, the same in both builds: the libc clocks,
# which are the machine's, and rand (platform/i_system.c); the files, which are
# the mounts and the machine's memory (platform/files.c)
WRAP_FLAGS := -Wl,--wrap=clock_gettime -Wl,--wrap=time -Wl,--wrap=gettimeofday -Wl,--wrap=clock \
	-Wl,--wrap=localtime -Wl,--wrap=rand -Wl,--wrap=srand \
	-Wl,--wrap=fopen -Wl,--wrap=access -Wl,--wrap=stat -Wl,--wrap=remove \
	-Wl,--wrap=fileno -Wl,--wrap=fstat -Wl,--wrap=opendir

# the patch series goes onto the submodule before anything of SRB2 builds
PATCH_STAMP := $(ROOT)/build/patches.stamp
$(PATCH_STAMP): $(wildcard $(ROOT)/patches/*.patch) apply-patches.sh
	sh apply-patches.sh
	@mkdir -p $(dir $@)
	@touch $@

$(PNGCONF): $(LIBS)/libpng-src/scripts/pnglibconf.h.prebuilt
	@mkdir -p $(dir $@)
	cp $< $@

# Every object depends on the flags it was built with: a change to them
# rebuilds it.
define flags_stamp
$(shell mkdir -p $(1); printf '%s\n' '$(2)' | cmp -s - $(1)/flags || printf '%s\n' '$(2)' > $(1)/flags)
endef
