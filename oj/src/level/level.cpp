
/**
 *
 * @file level.cpp
 *
 * Part of the OpenJazz project
 *
 * @par History:
 * - 23rd August 2005: Created level.c
 * - 3rd February 2009: Renamed level.c to level.cpp
 * - 19th July 2009: Created levelframe.cpp from parts of level.cpp
 * - 19th July 2009: Added parts of levelload.cpp to level.cpp
 * - 30th March 2010: Created baselevel.cpp from parts of level.cpp and
 *                  levelframe.cpp
 * - 1st August 2012: Renamed baselevel.cpp to level.cpp
 *
 * @par Licence:
 * Copyright (c) 2005-2017 AJ Thomson
 * Copyright (c) 2015-2026 Carsten Teibes
 *
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 * @par Description:
 * Deals with functionality common to ordinary levels and bonus levels.
 *
 */


#include "level.h"
#ifdef OJ32X
#include "oj32x.h"
extern "C" {
#include "plat.h"
}
#endif

#include "game/game.h"
#include "io/controls.h"
#include "io/gfx/font.h"
#include "io/gfx/sprite.h"
#include "io/gfx/video.h"
#include "io/sound.h"
#include "player/player.h"
#include "jj1/scene/jj1scene.h"
#include "loop.h"
#include "setup.h"
#include "debug.h"

/**
 * Create a new base level
 */
Level::Level (Game* owner) :
	menuOptions{"continue game", "save game", "load game", "setup options",
		"quit game", "debug menus", "debug menus", "debug menus", "debug menus"} {

	game = owner;

#if OJ_DEBUG
	nMenuOptions = 6;
#else
	nMenuOptions = 5;
#endif
#if defined(OJ32X_MARS) && defined(OJ32X_SCROLL)
	// PORT32X (J23b): the second SH2's game logic on / off, last in the pause menu (as the
	// Hurrican CD32X port's 2ND CPU); the debug menus move one place down
	menuOptions[nMenuOptions + 2] = menuOptions[5];
	menuOptions[5] = plat_slave_logic_on() ? "second cpu on" : "second cpu off";
	menuOptions[6] = oj32x_sky_on ? "background on" : "background off";   // J23b: the sky
	menuOptions[7] = oj32x_long_reads() ? "fast reads on" : "fast reads off";   // J24: J20's 32-bit reads
	nMenuOptions += 3;
#endif

	// Arbitrary initial value
	smoothfps = 50.0f;
	paletteEffects = NULL;
	paused = false;

	// Set the level stage
	stage = LS_NORMAL;
	sprites = tickOffset = steps = prevTicks = ticks = endTime = items = stats = 0;
#ifdef OJ32X_MARS
	// PORT32X: the frame counter (Z) starts as it was last left - kept in the RAM cart's
	// save area, on until the player switches it off
	if (oj32x_port_setting(0)) stats = S_SCREEN;
#endif
	multiplayer = false;
}


/**
 * Destroy base level
 */
Level::~Level () {

	stopMusic();

	if (paletteEffects) delete paletteEffects;

}


/**
 * Set the players' initial values.
 *
 * @param levelType The type of level for which to create a level player
 * @param anims New level player animations
 * @param flippedAnims New level player flipped animations
 * @param checkpoint Whether or not a checkpoint is in use
 * @param x The level players' new grid x-coordinate
 * @param y The level players' new grid y-coordinate
 */
void Level::createLevelPlayers (LevelType levelType, Anim** anims,
	Anim** flippedAnims, bool checkpoint, unsigned char x, unsigned char y) {

	int count;

	if (!checkpoint) game->setCheckpoint(x, y);

	for (count = 0; count < nPlayers; count++) {

		players[count].createLevelPlayer(levelType, anims, flippedAnims, x, y);
		game->resetPlayer(players + count);

	}

}


/**
 * Play a cutscene.
 *
 * @param file File name of the cutscene to be played
 *
 * @return Error code
 */
int Level::playScene (const char* file) {

	JJ1Scene* scene;
	int ret;

#ifdef OJ32X_MARS
	// PORT32X: no cutscenes on the 32X. Their decoder is far too slow on the SH2 and the
	// larger scenes do not fit the memory; the game goes straight on.
	(void)file;
	return E_NONE;
#endif

	delete paletteEffects;
	paletteEffects = NULL;

	try {

		scene = new JJ1Scene(file);

	} catch (int e) {

		return e;

	}

	ret = scene->play();

	delete scene;

	return ret;

}


/**
 * Perform timing calculations.
 */
void Level::timeCalcs () {

	// Calculate smoothed fps
#ifdef OJ32X
	// PORT32X R7: from the real time between frames. The level's ticks advance at most 100 ms a
	// frame (below), so the counter could never show less than 10 - a slower frame rate read 10.
	{
		static unsigned int lastGlobal;
		const unsigned int dt = globalTicks - lastGlobal;
		lastGlobal = globalTicks;
		if (dt < 2000)
			smoothfps = smoothfps + 1.0f - (smoothfps * ((float)dt) / 1000.0f);
	}
#else
	smoothfps = smoothfps + 1.0f -
		(smoothfps * ((float)(ticks - prevTicks)) / 1000.0f);
#endif
	/* This equation is a simplified version of
	(fps * c) + (smoothfps * (1 - c))
	where c = (1 / fps)
	and fps = 1000 / (ticks - prevTicks)
	In other words, the response of smoothFPS to changes in FPS decreases as the
	framerate increases
	The following version is for c = (1 / smoothfps)
	*/
	// smoothfps = (fps / smoothfps) + smoothfps - 1;

	// Ignore outlandish values
	if (smoothfps > 9999.0f) smoothfps = 9999.0f;
	if (smoothfps < 1.0f) smoothfps = 1.0f;


	// Track number of ticks of gameplay since the level started

	if (paused) {

		tickOffset = globalTicks - ticks;

	} else if (globalTicks - tickOffset > ticks + 100) {

		prevTicks = ticks;
		ticks += 100;

		tickOffset = globalTicks - ticks;

	} else {

		prevTicks = ticks;
		ticks = globalTicks - tickOffset;

	}

}


/**
 * Calculate the amount of time since the last completed step.
 *
 * @return Time since last step
 */
int Level::getTimeChange () {

	return paused? 0: ticks - ((steps * (setup.slowMotion? 100: 50)) / 3);

}


/**
 * Display menu (if visible) and statistics.
 *
 * @param bg Palette index of the box(es)
 * @param menu Whether or not the level menu should be displayed
 * @param option Selected menu uption
 * @param textPalIndex The first palette index for unseleceted text
 * @param selectedTextPalIndex The first palette index for selected text
 * @param textPalSpan The number of palette indices for text
 */
void Level::drawOverlay (unsigned char bg, bool menu, int option,
	unsigned char textPalIndex, unsigned char selectedTextPalIndex,
	int textPalSpan) {

	int count, width;

	// Draw graphics statistics

	if (stats & S_SCREEN) {
		if (video.getScaleFactor() > MIN_SCALE)
			video.drawRect(canvasW - 84, 11, 80, 37, bg);
		else
			video.drawRect(canvasW - 84, 11, 80, 25, bg);

#ifdef OJ32X_LONG_READS
		// PORT32X: the top line shows the blits' read width instead of the screen size:
		// 32 = 32-bit source reads on, 8 = byte reads (the Mode button switches them)
		panelBigFont->showNumber(oj32x_long_reads() ? 32 : 8, canvasW - 12, 14);
#else
		panelBigFont->showNumber(video.getWidth(), canvasW - 52, 14);
		panelBigFont->showString("x", canvasW - 48, 14);
		panelBigFont->showNumber(video.getHeight(), canvasW - 12, 14);
#endif
		panelBigFont->showString("fps", canvasW - 76, 26);
		panelBigFont->showNumber((int)smoothfps, canvasW - 12, 26);

		if (video.getScaleFactor() > MIN_SCALE) {
			panelBigFont->showNumber(canvasW, canvasW - 52, 38);
			panelBigFont->showString("x", canvasW - 48, 39);
			panelBigFont->showNumber(canvasH, canvasW - 12, 38);
		}
	}

	// Draw player list

	if (stats & S_PLAYERS) {

		width = 39;

		for (count = 0; count < nPlayers; count++)
			if (panelBigFont->getStringWidth(players[count].getName()) > width)
				width = panelBigFont->getStringWidth(players[count].getName());

		video.drawRect((canvasW >> 1) - 48, 11, width + 57, (nPlayers * 12) + 1, bg);

		for (count = 0; count < nPlayers; count++) {

			panelBigFont->showNumber(count + 1,
				(canvasW >> 1) - 24, 14 + (count * 12));
			panelBigFont->showString(players[count].getName(),
				(canvasW >> 1) - 16, 14 + (count * 12));
			panelBigFont->showNumber(players[count].teamScore,
				(canvasW >> 1) + width + 1, 14 + (count * 12));

		}

	}


	if (menu) {

		// Draw the menu

		int menuHeight = 16 * nMenuOptions + 12;

		video.drawRect((canvasW >> 1) - 72, ((canvasH - menuHeight) >> 1) - 10, 140, menuHeight, bg);
		video.drawRect((canvasW >> 1) - 72, ((canvasH - menuHeight) >> 1) - 10, 140, menuHeight, textPalIndex, false);


		for (count = 0; count < nMenuOptions; count++) {

			// Gray out Load and Save in multiplayer

			if (multiplayer && (count == 1 || count == 2)) {

				video.drawRect((canvasW >> 1) - 68, (canvasH >> 1) + (count << 4) - menuHeight / 2 - 2, 132, 15, textPalIndex);

			}

			if (count == option) fontmn2->mapPalette(240, 8, selectedTextPalIndex, textPalSpan);
			else fontmn2->mapPalette(240, 8, textPalIndex, textPalSpan);

			fontmn2->showString(menuOptions[count], (canvasW >> 1) - 64, (canvasH >> 1) + (count << 4) - menuHeight / 2);

		}

		fontmn2->restorePalette();

	}

}


/**
 * Process in-game menu selection.
 *
 * @param menu Whether or not the level menu should be displayed
 * @param option Chosen menu option
 *
 * @return Error code
 */
int Level::select (bool& menu, int option) {

	switch (option) {

		case 0: // Continue

			menu = false;
			playConfirmSound();

			break;

		case 1: // Save

			if (!multiplayer) {

				playConfirmSound();

				int ret = fileMenu.run(true, false);
				if (ret == E_QUIT) return E_QUIT;
				if (ret >= 0) {
					if (handleSave(ret))
						playConfirmSound();
					else
						playSound(SE::OW);
				}

				// Restore level palette
				video.setPalette(palette);
#ifdef OJ32X_MARS
				// PORT32X: clear the screen together with the palette change. Otherwise the
				// next frame shows the menu's picture in the level's colours - unseen at
				// 60 fps, a visible flash at the 32X's frame rate.
				video.setClipRect(canvas, nullptr);
				video.clearScreen(0);
#endif

			} else
				playSound(SE::OW);

			break;

		case 2: // Load

			if (!multiplayer) {

				playConfirmSound();

				int ret = fileMenu.run(false, false);
				if (ret == E_QUIT) return E_QUIT;
				if (ret >= 0) {
					playConfirmSound();
					return E_LOAD0 + ret;
				}

				// Restore level palette
				video.setPalette(palette);
#ifdef OJ32X_MARS
				video.setClipRect(canvas, nullptr);   // PORT32X: as above
				video.clearScreen(0);
#endif

			} else
				playSound(SE::OW);

			break;

		case 3: // Setup

			if (!multiplayer) {

				playConfirmSound();

				bool wasSlow = setup.slowMotion;

				if (setupMenu.run() == E_QUIT) return E_QUIT;

				if (wasSlow && !setup.slowMotion) steps <<= 1;
				else if (!wasSlow && setup.slowMotion) steps >>= 1;

				// Restore level palette
				video.setPalette(palette);
#ifdef OJ32X_MARS
				video.setClipRect(canvas, nullptr);   // PORT32X: as above
				video.clearScreen(0);
#endif

			} else
				playSound(SE::OW);

			break;

		case 4: // Quit game

			playConfirmSound();
			return E_RETURN;

#if defined(OJ32X_MARS) && defined(OJ32X_SCROLL)
		case 5: // PORT32X (J23b): second SH2 on / off - the menu stays open

			plat_slave_logic(!plat_slave_logic_on());
			menuOptions[5] = plat_slave_logic_on() ? "second cpu on" : "second cpu off";
			playConfirmSound();

			break;

		case 6: // PORT32X (J23b): the sky (gradient + sun / moon) on / off - off: one plain
			// colour, no sun, no colour turning (oj32x_sky_on)

			oj32x_sky_on = !oj32x_sky_on;
			menuOptions[6] = oj32x_sky_on ? "background on" : "background off";
			playConfirmSound();

			break;

		case 7: // PORT32X (J24): J20's 32-bit source reads on / off - to see whether the holes
			// in the clouds (console only) come from them

			oj32x_long_reads_toggle();
			menuOptions[7] = oj32x_long_reads() ? "fast reads on" : "fast reads off";
			playConfirmSound();

			break;
#endif

#if OJ_DEBUG
#if defined(OJ32X_MARS) && defined(OJ32X_SCROLL)
		case 8: // Debug menus
#else
		case 5: // Debug menus
#endif

			playConfirmSound();

			if (debugMenu.run(-1) == E_QUIT) return E_QUIT;

			// Restore level palette
			video.setPalette(palette);
#ifdef OJ32X_MARS
			video.setClipRect(canvas, nullptr);   // PORT32X: as above
			video.clearScreen(0);
#endif

			break;
#endif

	}

	return E_NONE;

}

/**
 * Process iteration.
 *
 * @param menu Whether or not the level menu should be displayed
 * @param option Selected menu uption
 * @param message Whether or not the "paused" message is being displayed
 *
 * @return Error code
 */
int Level::loop (bool& menu, int& option, bool& message) {

	int ret, x, y;

	// Networking
	if (multiplayer) {

		ret = game->step(ticks);

		if (ret < 0) return ret;

	}


	// Main loop
#ifdef OJ32X
	// PORT32X: the pad uses its play layout (A/B/C as jump and fire) while playing, and its
	// menu layout while the level's pause menu is open
	oj32xPlayLayout = !menu;
#endif
	if (::loop(NORMAL_LOOP, paletteEffects, paused) == E_QUIT) return E_QUIT;


	if (controls.release(C_ESCAPE)) {

		menu = !menu;
		option = 0;

	}

	if (controls.release(C_PAUSE)) message = !message;

	if (controls.release(C_STATS)) {

		if (!multiplayer) stats ^= S_SCREEN;
		else stats = (stats + 1) & 3;
#ifdef OJ32X_MARS
		// PORT32X: remember the frame counter's state across power-off (RAM cart save area)
		oj32x_port_setting_set(0, (stats & S_SCREEN) ? 1 : 0);
#endif

	}

	if (menu) {

		// Deal with menu controls

		if (controls.release(C_UP)) option = (option + nMenuOptions - 1) % nMenuOptions;

		if (controls.release(C_DOWN)) option = (option + 1) % nMenuOptions;

		if (controls.release(C_ENTER)) {

			ret = select(menu, option);

			if (ret < 0) return ret;

		}

		if (controls.getCursor(x, y)) {

			x -= (canvasW >> 1) - 64;
			y -= (canvasH >> 1) - 46;

			if ((x >= 0) && (x < 120) && (y >= 0) && (y < 96)) {

				option = y >> 4;

				if (controls.wasCursorReleased()) {

					ret = select(menu, option);

					if (ret < 0) return ret;

				}

			} else if (controls.wasCursorReleased()) menu = false;

		}

	} else {

		if (controls.wasCursorReleased()) menu = true;

	}

	if (!multiplayer) paused = message || menu;

	timeCalcs();

	return E_NONE;

}


/**
 * Add extra time.
 *
 * @param seconds Number of seconds to add
 */
void Level::addTimer (int seconds) {

	if (stage != LS_NORMAL) return;

	endTime += seconds * 1000;

	if (endTime >= ticks + (10 * 60 * 1000))
		endTime = ticks + (10 * 60 * 1000) - 1;

	if (multiplayer) {

		unsigned char buffer[MTL_L_PROP];
		buffer[0] = MTL_L_PROP;
		buffer[1] = MT_L_PROP;
		buffer[2] = 2; // add timer
		buffer[3] = seconds;
		buffer[4] = 0; // not used

		game->send(buffer);

	}

}


/**
 * Set the level stage.
 *
 * @param newStage New level stage
 */
void Level::setStage (LevelStage newStage) {

	if (stage == newStage) return;

	stage = newStage;

	if (multiplayer) {

		unsigned char buffer[MTL_L_STAGE];
		buffer[0] = MTL_L_STAGE;
		buffer[1] = MT_L_STAGE;
		buffer[2] = stage;
		game->send(buffer);

	}

}


/**
 * Determine the current level stage.
 *
 * @return The current level stage.
 */
LevelStage Level::getStage () {

	return stage;

}
