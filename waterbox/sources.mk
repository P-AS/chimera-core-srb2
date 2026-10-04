# sources.mk - what native.mk and guest.mk both build: the same files with the
# same defines, so the native reference and the sandboxed core are the same
# program. Included, not run.

ROOT := ..
SRB2 := $(ROOT)/extern/SRB2/src
LIBS := $(ROOT)/extern/SRB2/libs
OGG := $(ROOT)/extern/ogg
VORBIS := $(ROOT)/extern/vorbis
GME := $(ROOT)/extern/gme/gme
OPENMPT := $(ROOT)/extern/openmpt
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
# Taken from upstream's dummy/ interface as it is: i_net.c (no network),
# i_cdmus.c (no CD); the sound is the core's own (platform/i_sound.c); from sdl/,
# dosstr.c (strupr, strlwr). md5.c and apng.c are upstream's optional files,
# in as its Linux build has them.
srb2_list = $(addprefix $(SRB2)/$(1),$(filter %.c,$(shell cat $(SRB2)/$(1)Sourcefile)))
SRB2_EXCLUDE := $(SRB2)/comptime.c $(SRB2)/netcode/i_tcp.c
SRB2_SRCS := $(filter-out $(SRB2_EXCLUDE), \
	$(call srb2_list,) $(call srb2_list,blua/) $(call srb2_list,netcode/) \
	$(SRB2)/md5.c $(SRB2)/apng.c $(SRB2)/sdl/dosstr.c $(SRB2)/dummy/i_net.c $(SRB2)/dummy/i_cdmus.c)
# upstream's defines (src/CMakeLists.txt, a Linux x86-64 build): software
# renderer only (no HWRENDER), no threads, no curl, no UPnP, no Mumble;
# libopenmpt (HAVE_OPENMPT: s_sound.c's module handle and its filter setting,
# the sound menu's OpenMPT section, as upstream's Linux builds have them);
# zlib (the .pk3 files) and libpng (PNG graphics in them) from upstream's own
# libs/. C23, with -fwrapv: the game relies on wrapping
# signed arithmetic. NDEBUG, as upstream's release build and the guest have it.
# MAXVIDWIDTH/HEIGHT 3840x2160 (patches/0005): the resolution setting's largest.
SRB2_DEFS := -DNDEBUG -DMAXVIDWIDTH=3840 -DMAXVIDHEIGHT=2160 -DUNIXCOMMON -DLINUX -DLINUX64 -D_LARGEFILE64_SOURCE -DHAVE_ZLIB -DHAVE_PNG -DHAVE_OPENMPT \
	-DNOMUMBLE -DNOEXECINFO -DNOUPNP
SRB2_INCS := -Iplatform -Icompat -I$(OGG)/include -I$(VORBIS)/include -I$(GME)/.. -I$(OPENMPT) -I$(SRB2) -I$(SRB2)/blua -I$(LIBS)/zlib -I$(LIBS)/libpng-src -I$(PNGCONF_DIR)
SRB2_CFLAGS_COMMON := -std=gnu23 -O2 -fwrapv -fno-strict-aliasing $(SRB2_DEFS) $(SRB2_INCS)

# ---- libogg and libvorbis (the submodules extern/ogg, v1.3.5, and
# extern/vorbis, v1.3.7): Ogg Vorbis, the sounds' and the music's; the
# decoder's files (no encoder, no tools). libogg's configure-made
# config_types.h is compat/ogg/'s.
OGG_SRCS := $(OGG)/src/bitwise.c $(OGG)/src/framing.c
VORBIS_NAMES := analysis bitrate block codebook envelope floor0 floor1 info lookup lpc lsp mapping0 mdct psy \
	registry res0 sharedbook smallft synthesis vorbisfile window
VORBIS_SRCS := $(addprefix $(VORBIS)/lib/,$(addsuffix .c,$(VORBIS_NAMES)))
XIPH_CFLAGS_COMMON := -std=gnu11 -O2 -DNDEBUG -Icompat -I$(OGG)/include -I$(VORBIS)/include -I$(VORBIS)/lib

# ---- Game_Music_Emu (the submodule extern/gme, libgme 0.6.5): VGM/VGZ, NSF,
# SPC, GBS, HES, KSS, AY, SAP, GYM music and sounds. Every emulator, as its
# CMake builds them by default; the Nuked YM2612 (its default); no zlib of its
# own (SRB2 inflates a VGZ itself). C++, without exceptions or RTTI (it uses
# neither: blargg's allocations are new (std::nothrow)).
GME_NAMES := Blip_Buffer Classic_Emu Data_Reader Dual_Resampler Effects_Buffer Fir_Resampler gme Gme_File \
	M3u_Playlist Multi_Buffer Music_Emu Ay_Apu Ym2612_Nuked Sms_Apu Ay_Cpu Ay_Emu Gb_Apu Gb_Cpu Gb_Oscs \
	Gbs_Emu Gym_Emu Hes_Apu Hes_Apu_Adpcm Hes_Cpu Hes_Emu Kss_Cpu Kss_Emu Kss_Scc_Apu Nes_Apu Nes_Cpu \
	Nes_Fme7_Apu Nes_Namco_Apu Nes_Oscs Nes_Vrc6_Apu Nes_Fds_Apu Nes_Vrc7_Apu Nsf_Emu Nsfe_Emu Sap_Apu \
	Sap_Cpu Sap_Emu Snes_Spc Spc_Cpu Spc_Dsp Spc_Emu Spc_Filter Vgm_Emu Vgm_Emu_Impl Ym2413_Emu
GME_SRCS := $(addprefix $(GME)/,$(addsuffix .cpp,$(GME_NAMES))) $(GME)/ext/emu2413.c
GME_DEFS := -DNDEBUG -DVGM_YM2612_NUKED -DBLARGG_LITTLE_ENDIAN=1
# -include ctime: Hes_Emu.cpp names time_t (for its emulated time), which
# glibc's headers declare in passing and musl's do not
GME_CXXFLAGS_COMMON := -std=gnu++17 -O2 -fno-exceptions -fno-rtti -include ctime $(GME_DEFS) -I$(GME)
GME_CFLAGS_COMMON := -std=gnu11 -O2 $(GME_DEFS) -I$(GME)

# ---- libopenmpt (the submodule extern/openmpt, libopenmpt-0.8.9, shallow;
# its patches/openmpt/ series): tracker modules (MOD, S3M, XM, IT, MPTM and
# the rest). The sources are its own Makefile's (the library, its common and
# sound code, the DMO plugins it emulates); C++17 with exceptions and RTTI, as
# it is built; its own settings leave the SIMD intrinsics out. Ogg Vorbis and
# zlib (MO3's compressed samples, archives) are the core's. Its random
# seeding is deterministic (MPT_BUILD_DETERMINISTIC_RANDOM, patch 0001).
OPENMPT_DIRS := src/openmpt/all src/openmpt/base src/openmpt/logging src/openmpt/random common \
	src/openmpt/fileformat_base src/openmpt/soundbase src/openmpt/soundfile_data soundlib soundlib/plugins \
	soundlib/plugins/dmo sounddsp libopenmpt
OPENMPT_SRCS := $(foreach d,$(OPENMPT_DIRS),$(sort $(wildcard $(OPENMPT)/$(d)/*.cpp)))
OPENMPT_DEFS := -DNDEBUG -DLIBOPENMPT_BUILD -DMPT_BUILD_DETERMINISTIC_RANDOM -DMPT_WITH_ZLIB -DMPT_WITH_OGG \
	-DMPT_WITH_VORBIS -DMPT_WITH_VORBISFILE
OPENMPT_CXXFLAGS_COMMON := -std=gnu++17 -O2 -fexceptions -frtti $(OPENMPT_DEFS) -I$(OPENMPT)/src -I$(OPENMPT)/common \
	-I$(OPENMPT) -Icompat -I$(OGG)/include -I$(VORBIS)/include -I$(LIBS)/zlib
OPENMPT_STAMP := $(ROOT)/build/patches-openmpt.stamp
$(OPENMPT_STAMP): $(wildcard $(ROOT)/patches/openmpt/*.patch) apply-patches.sh
	sh apply-patches.sh openmpt
	@mkdir -p $(dir $@)
	@touch $@

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
PLATFORM_NAMES := i_system i_video i_sound i_threads i_net files detmath comptime
CORE_C_NAMES := $(addprefix platform/,$(PLATFORM_NAMES)) srb2-driver srb2-input wbx-entry
# libco (miniBox's extern/libco, public domain): the engine's cothread
LIBCO_SRC := $(MB)/extern/libco/amd64.c
CORE_HDRS := $(wildcard *.h) $(wildcard platform/*.h) $(wildcard compat/*/*.h)
CORE_CFLAGS_COMMON := $(SRB2_CFLAGS_COMMON) -I$(MB)/extern/libco

# the calls the core answers itself, the same in both builds: the libc clocks,
# which are the machine's, and rand (platform/i_system.c); the files, which are
# the mounts and the machine's memory (platform/files.c); the inexact math
# (platform/detmath.c)
WRAP_FLAGS := -Wl,--wrap=clock_gettime -Wl,--wrap=time -Wl,--wrap=gettimeofday -Wl,--wrap=clock \
	-Wl,--wrap=localtime -Wl,--wrap=rand -Wl,--wrap=srand \
	-Wl,--wrap=fopen -Wl,--wrap=access -Wl,--wrap=stat -Wl,--wrap=remove \
	-Wl,--wrap=fileno -Wl,--wrap=fstat -Wl,--wrap=opendir \
	-Wl,--wrap=sin -Wl,--wrap=cos -Wl,--wrap=acos -Wl,--wrap=atan -Wl,--wrap=exp -Wl,--wrap=log \
	-Wl,--wrap=hypot -Wl,--wrap=sincos -Wl,--wrap=pow -Wl,--wrap=log2 -Wl,--wrap=sinf -Wl,--wrap=cosf \
	-Wl,--wrap=tanf -Wl,--wrap=logf -Wl,--wrap=log10f -Wl,--wrap=powf -Wl,--wrap=sincosf

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
