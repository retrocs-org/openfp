# SPDX-License-Identifier: MIT
# AmigaOS 3.x game and shell tool (verify, bench, net). The game release targets 68040.
# With vbcc or Bebbo's gcc, whichever is installed (vbcc first):
#
#   make -f src/platform/amiga/Makefile                       (from the project's top folder)
#   make -f src/platform/amiga/Makefile CPU=68060             (for a 68060-specific test build)
#   make -f src/platform/amiga/Makefile COMPILER=gcc          (or COMPILER=vbcc: this one)
#   make -f src/platform/amiga/Makefile online local         (the 16-colour release clients)
#   make -f src/platform/amiga/Makefile NDK=/path/to/NDK3.2/Include_H
#
# vbcc: vc +aos68k (VBCC set: Homebrew's tditlu/amiga/vbcc, VBCC=$(brew --prefix vbcc)) and the NDK's
#   C headers (NDK=); the sockets are bsdsocket_vbcc.h's, so no Roadshow SDK is needed.
# gcc: Bebbo's m68k-amigaos-gcc with libnix: its "make all" installs the NDK and Roadshow's headers.
CPU     ?= 68040
BUILD_CPU = $(CPU)
CPU_SUFFIX = $(if $(filter 68040,$(CPU)),,-$(CPU))
# Keep the small grid in the offline game and the shell tool. The editor and
# online game can author and play Huge maps.
GRID        ?= -DOFP_MAP_MAX=128
ONLINE_GRID ?= -DOFP_MAP_MAX=192
AMISSLINC ?=
NETINC  ?=
NDK     ?=
ifeq ($(origin COMPILER),undefined)
COMPILER := $(if $(shell command -v vc 2>/dev/null),vbcc,gcc)
endif
VCONFIG ?= aos68k
# (vbcc: 153 "statement has no effect" is (void)x)
ifeq ($(COMPILER),vbcc)
CC       = vc +$(VCONFIG)
OPT     ?= -O1
CFLAGS  ?= $(OPT)
CFLAGS  += $(GRID) -c99 -cpu=$(BUILD_CPU) -D__amigaos__ -Isrc $(if $(NDK),-I$(NDK)) $(if $(NETINC),-I$(NETINC)) -dontwarn=153
LDFLAGS += -lamiga
else
ifeq ($(origin CC),default)
CC       = $(if $(AMIGA_CC),$(AMIGA_CC),m68k-amigaos-gcc)
endif
# libnix: nothing needed beyond AmigaOS itself (LIBC= to test the build with another compiler)
LIBC    ?= -noixemul
CFLAGS  ?= -O2
CFLAGS  += $(GRID) -std=gnu99 -m$(BUILD_CPU) $(if $(filter 68040 68060,$(BUILD_CPU)),-msoft-float) $(LIBC) -Wall -Wextra -Isrc $(if $(NETINC),-I$(NETINC))
LDFLAGS += $(LIBC)
endif
# One release renderer: four bitplanes (16 colours) on OCS, ECS and AGA hardware.
# Accept the old AGA=0 command line, but never silently build a higher-colour client.
AGA ?= 0
AGA_COLOURS ?= 16
ifneq ($(AGA),0)
$(error Only the 16-colour Amiga client is supported; omit AGA or use AGA=0)
endif
ifneq ($(AGA_COLOURS),16)
$(error Only AGA_COLOURS=16 is supported in this release)
endif
ART = src/platform/amiga/classic_art.h src/platform/amiga/world_art.h src/platform/amiga/original_terrain.h
OUT ?= build-amiga-ecs$(CPU_SUFFIX)

CORE   = src/core/fixed.c src/core/world.c src/core/worldgen.c src/core/game.c src/core/ai.c src/core/mapfile.c src/core/match.c
COMMON = src/common/replay.c src/common/net.c
AMIGA  = src/platform/amiga/ofp68k.c src/platform/amiga/net_amiga.c src/platform/amiga/amiga_clock.c
OBJS   = $(patsubst src/%.c,$(OUT)/%.o,$(CORE) $(COMMON) $(AMIGA))

MUSIC  = src/platform/amiga/amiga_music.c src/platform/amiga/modplay.c
# the client: local.c and its parts in local/, each its own translation unit (what they share: local/internal.h)
CLIENT = src/platform/amiga/local.c $(addprefix src/platform/amiga/local/,art.c world.c hud.c system.c)
LOCAL_OBJS = $(patsubst src/%.c,$(OUT)/local/%.o,$(CORE) src/common/fx_budget.c $(CLIENT) src/platform/amiga/local/duel.c src/platform/amiga/amiga_clock.c src/platform/amiga/amiga_sound.c $(MUSIC))
ONLINE_PARTS = $(patsubst src/%.c,$(OUT)/online/%.o,$(CLIENT) $(addprefix src/platform/amiga/local/,menus.c replays.c editor.c title.c online.c))
CLIENT_HEADERS = src/platform/amiga/surface_art.h src/platform/amiga/local/internal.h src/platform/amiga/local/pacing.h src/platform/amiga/local/profile.h src/common/operation.h src/common/mapcheck.h src/common/soundtrack.h src/common/credits.h $(ART) src/platform/amiga/vehicle_art.h src/platform/amiga/faces_art.h src/platform/amiga/classic_maps.h \
                 src/platform/amiga/menu_settings.h src/common/volume.h src/platform/amiga/sounds_8bit.h src/platform/amiga/music_mods.h

ONLINE_COMMON = $(patsubst src/%.c,$(OUT)/online/%.o,$(CORE) src/common/fx_budget.c src/common/recovery.c src/common/net.c src/common/client.c src/common/rules.c src/common/sha256.c src/common/training.c src/common/mapcheck.c src/common/operation.c src/common/replay.c src/platform/amiga/menu_settings.c src/platform/amiga/public_servers.c src/platform/amiga/amiga_share.c src/platform/amiga/net_amiga.c src/platform/amiga/amiga_clock.c src/platform/amiga/amiga_sound.c $(MUSIC))
ONLINE_OBJS = $(ONLINE_COMMON) $(ONLINE_PARTS)
all: $(OUT)/ofp68k

ifneq ($(filter 68040 68060,$(CPU)),)
# Keep the entry point on the base instruction set so older CPUs can show the error.
GAME_ENTRY = $(OUT)/platform/amiga/game_entry.o
$(GAME_ENTRY): BUILD_CPU = 68000
$(GAME_ENTRY): CFLAGS += -DOFP_AMIGA_REQUIRED_CPU=$(CPU)
$(OUT)/local/platform/amiga/local/duel.o $(OUT)/online/platform/amiga/local/online.o: CFLAGS += -Dmain=amiga_game_main
endif

$(ONLINE_OBJS): GRID = $(ONLINE_GRID)

local: $(OUT)/OpenFirePower

online: $(OUT)/OpenFirePowerOnline

$(OUT)/OpenFirePowerOnline: $(GAME_ENTRY) $(ONLINE_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(ONLINE_PARTS): $(OUT)/online/%.o: src/%.c $(CLIENT_HEADERS) third_party/font8x8_basic.h src/platform/amiga/map_catalog.h \
                 src/common/client.h src/common/replay.h src/platform/amiga/public_servers.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DOFP_AMIGA_ONLINE -c -o $@ $<

$(ONLINE_COMMON): $(OUT)/online/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# The core structs are shared by the clients, replays and the Shell tool.
$(sort $(OBJS) $(LOCAL_OBJS) $(ONLINE_OBJS)): src/core/game.h src/core/match.h src/core/world.h
$(sort $(OBJS) $(LOCAL_OBJS) $(ONLINE_OBJS) $(GAME_ENTRY)): src/platform/amiga/Makefile

$(OUT)/common/client.o $(OUT)/online/common/client.o: src/common/client.h src/common/net.h
$(OUT)/common/net.o $(OUT)/platform/amiga/net_amiga.o $(OUT)/online/common/net.o $(OUT)/online/platform/amiga/net_amiga.o: src/common/net.h
$(OUT)/common/replay.o $(OUT)/platform/amiga/ofp68k.o $(OUT)/online/common/replay.o: src/common/replay.h
$(OUT)/platform/amiga/ofp68k.o: src/common/net.h
$(OUT)/online/platform/amiga/amiga_sound.o $(OUT)/local/platform/amiga/amiga_sound.o: src/platform/amiga/sounds_8bit.h src/platform/amiga/amiga_sound.h
$(OUT)/online/platform/amiga/amiga_music.o $(OUT)/local/platform/amiga/amiga_music.o: src/platform/amiga/music_mods.h src/platform/amiga/modplay.h src/platform/amiga/amiga_music.h src/common/soundtrack.h
$(OUT)/online/platform/amiga/modplay.o $(OUT)/local/platform/amiga/modplay.o: src/platform/amiga/modplay.h
$(OUT)/online/platform/amiga/menu_settings.o: src/platform/amiga/menu_settings.h src/platform/amiga/amiga_music.h
$(OUT)/online/platform/amiga/menu_settings.o $(filter %/amiga_music.o %/amiga_sound.o,$(LOCAL_OBJS) $(ONLINE_COMMON)): src/common/volume.h
$(OUT)/online/platform/amiga/public_servers.o: CFLAGS += $(if $(AMISSLINC),-I$(AMISSLINC))
$(OUT)/online/platform/amiga/public_servers.o: src/platform/amiga/public_servers.h src/platform/amiga/menu_settings.h
$(OUT)/online/platform/amiga/amiga_share.o: src/platform/amiga/public_servers.h src/common/net.h

$(OUT)/OpenFirePower: $(GAME_ENTRY) $(LOCAL_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(OUT)/ofp68k: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)
	@echo "built $@ ($(COMPILER), $(CPU)): copy it and tests/replays/*.ofprep to the Amiga, then: ofp68k verify #?.ofprep"

$(patsubst src/%.c,$(OUT)/local/%.o,$(CLIENT) src/platform/amiga/local/duel.c): $(CLIENT_HEADERS)

$(OUT)/local/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DOFP_LOCAL_DUEL -c -o $@ $<

$(OUT)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<
	@true                                     # (vbcc and gcc alike: -c file -o object)

clean:
	rm -rf $(OUT)

.PHONY: all clean local online
