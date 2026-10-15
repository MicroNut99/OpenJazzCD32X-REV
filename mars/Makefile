# OpenJazz CD32X - the SH2 engine (J4a): the game + port layer, linked as a 32X cartridge
# image for the CD32X loader (OpenLara's), with Tyrian's start-up code.
#   make -k -C mars 2>&1 | tee mars_log.txt
# Results: mars/openjazz32x.elf / .bin / .map, mars/OPENJAZZ.32X (via tools/make_oj_rom.py),
#          mars/report.txt (sizes).
# A PC dry run of the old trial link is no longer possible (SH2 start-up code).

TC      ?= /opt/toolchains/sega/sh-elf/bin/sh-elf-
CC      := $(TC)gcc
CXX     := $(TC)g++
SIZE    := $(TC)size
NM      := $(TC)nm
ARCH    ?= -m2 -mb

ROOT := ..
SRC  := $(ROOT)/oj/src
PORT := $(ROOT)/port
EXT  := $(ROOT)/oj/ext

# the same game files as host/Makefile
GAME_SRCS := \
	game/game.cpp game/gamemode.cpp game/localgame.cpp game/clientgame.cpp game/servergame.cpp \
	io/controls.cpp io/file.cpp io/log.cpp io/network.cpp io/sound.cpp \
	io/gfx/anim.cpp io/gfx/font.cpp io/gfx/paletteeffects.cpp io/gfx/sprite.cpp io/gfx/video.cpp \
	level/level.cpp level/movable.cpp main.cpp debug.cpp \
	menu/debugmenu.cpp menu/filemenu.cpp menu/gamemenu.cpp menu/mainmenu.cpp menu/menu.cpp \
	menu/plasma.cpp menu/setupmenu.cpp menu/credits32x.cpp platforms/platform_interface.cpp player/player.cpp \
	setup.cpp util.cpp version.cpp \
	jj1/jj1episodeutils.cpp jj1/bonuslevel/jj1bonuslevel.cpp jj1/bonuslevel/jj1bonuslevelplayer.cpp \
	jj1/level/jj1bird.cpp jj1/level/jj1levelplayer.cpp jj1/level/jj1levelplayerframe.cpp \
	jj1/level/event/jj1bridge.cpp jj1/level/event/jj1event.cpp jj1/level/event/jj1guardians.cpp \
	jj1/level/event/jj1standardevent.cpp jj1/level/jj1bullet.cpp jj1/level/jj1demolevel.cpp \
	jj1/level/jj1level.cpp jj1/level/jj1levelframe.cpp jj1/level/jj1levelload.cpp \
	jj1/planet/jj1planet.cpp jj1/save/jj1save.cpp jj1/scene/jj1scene.cpp jj1/scene/jj1sceneload.cpp
EXT_SRCS := argparse/argparse.c miniz/miniz.c stb/stb_rect_pack.c
XMP_SRCS := $(notdir $(wildcard $(EXT)/xmp/src/*.c)) $(addprefix loaders/,$(notdir $(wildcard $(EXT)/xmp/src/loaders/*.c)))
PORT_CXX := sdl_oj.cpp mars_boottest.cpp
PORT_C   := cart.c pack.c plat_mars.c mars_syscalls.c

OBJDIR := obj
OPT    := $(ARCH) -Os -ffunction-sections -fdata-sections
DEFS   := -DOJ32X -DOJ32X_MARS -DOJ_SDL1 -DPORTABLE -DNDEBUG \
          -DOJ_VERSION=\"git-oj32x\" -DOJ_DATE=\"2026-10-06\" -D__32X__ -DMARS \
          -DOJ_URL=\"http://alister.eu/jazz/oj/\" -DOJ_BUGREPORT=\"https://github.com/AlisterT/openjazz/issues\" \
          -DLIBXMP_CORE_PLAYER -DLIBXMP_STATIC -DXMP_SYM_VISIBILITY
INCS   := -I$(PORT)/include -I$(PORT) -I$(SRC) -I$(EXT)/argparse -I$(EXT)/miniz -I$(EXT)/xmp -I$(EXT)/xmp/src -I$(EXT)/stb
# J10: build.sh passes EXTRA_DEFS=-DOJ32X_MUSIC=1 when music/*.wav exist
DEFS += $(EXTRA_DEFS)
# J14: the frame profiler box in levels - off (J15d); add -DOJ32X_PROFILE to bring it back
# J15: the line-table scrolling renderer (remove for the old full-redraw renderer)
DEFS += -DOJ32X_SCROLL
# J16 speed switches (each can be removed on its own):
#   OJ32X_FAST_CODE   pixel routines in SDRAM, compiled for speed
#   OJ32X_PANEL_DIFF  status panel drawn in SDRAM, only changed rows copied to the frame buffer
#   OJ32X_FLIP_ASYNC  presenting only requests the swap; the logic runs while it waits
DEFS += -DOJ32X_FAST_CODE -DOJ32X_PANEL_DIFF -DOJ32X_FLIP_ASYNC
CXXFLAGS := -std=gnu++14 $(OPT) $(DEFS) $(INCS) -fno-threadsafe-statics -Wall -Wno-unused-result -Wno-narrowing -MMD -MP
CFLAGS   := -std=gnu11 $(OPT) $(DEFS) $(INCS) -w -MMD -MP

GAME_OBJS := $(addprefix $(OBJDIR)/game/,$(GAME_SRCS:.cpp=.o))
EXT_OBJS  := $(addprefix $(OBJDIR)/ext/,$(EXT_SRCS:.c=.o))
XMP_OBJS  := $(addprefix $(OBJDIR)/xmp/,$(XMP_SRCS:.c=.o))
PORT_OBJS := $(addprefix $(OBJDIR)/port/,$(PORT_CXX:.cpp=.o) $(PORT_C:.c=.o))
OBJS := $(GAME_OBJS) $(EXT_OBJS) $(XMP_OBJS) $(PORT_OBJS)

LD_SCRIPT := $(PORT)/mars/oj.ld

all: OPENJAZZ.32X report.txt
	@echo; cat report.txt

openjazz32x.elf: $(OBJDIR)/crt0_oj.o $(OBJS) $(LD_SCRIPT)
	$(CXX) $(ARCH) -nostartfiles -T $(LD_SCRIPT) -Wl,--gc-sections -Wl,-Map,openjazz32x.map \
		-o $@ $(OBJDIR)/crt0_oj.o $(OBJS) -lstdc++ -lm -lc -lgcc

openjazz32x.bin: openjazz32x.elf
	$(TC)objcopy -O binary -j .sdcode -j .data -j .text $< $@

OPENJAZZ.32X: openjazz32x.elf openjazz32x.bin $(ROOT)/tools/make_oj_rom.py $(ROOT)/tools/mars_boot.bin
	python3 $(ROOT)/tools/make_oj_rom.py $(ROOT)/tools/mars_boot.bin openjazz32x.elf openjazz32x.bin $@

report.txt: openjazz32x.elf
	@{ echo "== SH2 sections (bytes)"; $(SIZE) -A openjazz32x.elf | grep -E "sdcode|data|text|bss|Total"; } > $@

$(OBJDIR)/crt0_oj.o: $(PORT)/mars/crt0_oj.s
	@mkdir -p $(dir $@)
	$(CC) $(ARCH) -c $< -o $@

$(OBJDIR)/game/%.o: $(SRC)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(OBJDIR)/ext/%.o: $(EXT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(if $(findstring stb_rect_pack,$<),-include $(PORT)/oj32x_sort.h -DSTBRP_SORT=oj32x_stable_sort) -c $< -o $@
$(OBJDIR)/xmp/%.o: $(EXT)/xmp/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJDIR)/port/%.o: $(PORT)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(OBJDIR)/port/%.o: $(PORT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf obj openjazz32x.elf openjazz32x.bin openjazz32x.map OPENJAZZ.32X report.txt
.PHONY: all clean

# J4c3: a changed Makefile (compiler settings) rebuilds every object
$(OBJS) $(OBJDIR)/crt0_oj.o: Makefile

# J7: header dependencies. Without them a changed header (e.g. jj1level.h, file.h) did not
# rebuild the files that include it: objects with different ideas of a class's size were
# linked together (J7: `new JJ1Level` still asked for the old 156,736 bytes).
-include $(OBJS:.o=.d)
