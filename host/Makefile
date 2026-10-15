# OpenJazz CD32X - PC test build (no SDL needed).
# The game (oj/src, Jazz 1 only, no network, no scaling) + the SDL 1.2 shim
# (port/sdl_oj.cpp) + Tyrian's PC platform layer (port/plat_host.c).
#   make -C host        -> host/openjazz32x_host
#   make -C host ASAN=1 -> with AddressSanitizer (crashes, overruns)

ROOT := ..
SRC  := $(ROOT)/oj/src
PORT := $(ROOT)/port
EXT  := $(ROOT)/oj/ext

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
PORT_CXX := sdl_oj.cpp host_new.cpp
PORT_C   := plat_host.c mixer.c cart.c pack.c pack_bake.c

OBJDIR := obj$(if $(ASAN),_asan)
OPT    := $(if $(ASAN),-O1 -g -fsanitize=address -fno-omit-frame-pointer,-O2 -g)
OPT    += -fno-pie
DEFS   := -DOJ32X -DOJ_SDL1 -DPORTABLE -DNDEBUG \
          -DOJ_VERSION=\"git-oj32x\" -DOJ_DATE=\"2026-10-05\" \
          -DOJ_URL=\"http://alister.eu/jazz/oj/\" -DOJ_BUGREPORT=\"https://github.com/AlisterT/openjazz/issues\" \
          -DLIBXMP_CORE_PLAYER -DLIBXMP_STATIC -DXMP_SYM_VISIBILITY
INCS   := -I$(PORT)/include -I$(PORT) -I$(SRC) -I$(EXT)/argparse -I$(EXT)/miniz -I$(EXT)/xmp -I$(EXT)/xmp/src -I$(EXT)/stb
# -Wno-narrowing: SDL 1.2 rectangles are 16-bit, font.cpp fills them from ints (as with real SDL 1.2)
CXXFLAGS := -std=gnu++14 $(OPT) $(DEFS) $(INCS) -Wall -Wno-unused-result -Wno-narrowing -MMD -MP
CFLAGS   := -std=gnu11 $(OPT) $(DEFS) $(INCS) -w -MMD -MP

OBJS := $(addprefix $(OBJDIR)/game/,$(GAME_SRCS:.cpp=.o)) \
        $(addprefix $(OBJDIR)/ext/,$(EXT_SRCS:.c=.o)) \
        $(addprefix $(OBJDIR)/xmp/,$(XMP_SRCS:.c=.o)) \
        $(addprefix $(OBJDIR)/port/,$(PORT_CXX:.cpp=.o) $(PORT_C:.c=.o))

BIN := openjazz32x_host$(if $(ASAN),_asan)
$(BIN): $(OBJS)
	$(CXX) $(OPT) -no-pie -o $@ $^ -lm -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free

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

-include $(OBJS:.o=.d)

clean:
	rm -rf obj obj_asan openjazz32x_host openjazz32x_host_asan test_sdl_oj
.PHONY: clean

# shim checks (no game data needed)
test: test_sdl_oj
	./test_sdl_oj
test_sdl_oj: test_sdl_oj.cpp $(OBJDIR)/port/sdl_oj.o $(OBJDIR)/port/host_new.o $(OBJDIR)/port/plat_host.o $(OBJDIR)/port/mixer.o $(OBJDIR)/port/cart.o $(OBJDIR)/port/pack.o $(OBJDIR)/port/pack_bake.o
	$(CXX) $(CXXFLAGS) -no-pie -o $@ $^ -lm -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free
.PHONY: test

# J4c3: a changed Makefile (compiler settings) rebuilds every object
$(OBJS): Makefile
