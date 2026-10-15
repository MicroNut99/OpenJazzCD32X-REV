
/**
 *
 * @file jj1levelload.cpp
 *
 * Part of the OpenJazz project
 *
 * @par History:
 * - 23rd August 2005: Created level.c
 * - 22nd July 2008: Created levelload.c from parts of level.c
 * - 3rd February 2009: Renamed levelload.c to levelload.cpp
 * - 18th July 2009: Created demolevel.cpp from parts of level.cpp and
 *                 levelload.cpp
 * - 19th July 2009: Added parts of levelload.cpp to level.cpp
 * - 28th June 2010: Created levelloadjj2.cpp from parts of levelload.cpp
 * - 1st August 2012: Renamed levelload.cpp to jj1levelload.cpp
 *
 * @par Licence:
 * Copyright (c) 2005-2017 AJ Thomson
 *
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 * @par Description:
 * Deals with the loading of level data.
 *
 */


#include "jj1bullet.h"
#include "event/jj1event.h"
#include "jj1level.h"
#include <cstdlib>
#include <cstdio>
#include "jj1levelplayer.h"

#include "game/game.h"
#include "io/file.h"
#include "io/gfx/font.h"
#include "io/gfx/sprite.h"
#include "io/gfx/video.h"
#include "io/sound.h"
#include "loop.h"
#include "util.h"
#include "io/log.h"
#ifdef OJ32X
#include "oj32x.h"
#include "genbg.h"
#endif

#include <string.h>


#define SKEY 254 /* Sprite colour key */


/**
 * Load the HUD graphical data.
 *
 * @return Error code
 */
int JJ1Level::loadPanel () {

	FilePtr file;
	try {

		file = std::make_unique<File>("PANEL.000", PATH_TYPE_GAME);

	} catch (int e) {

		return e;

	}

#ifdef OJ32X
	// PORT32X: the panel's 46 KB come decoded from the cart pack. Decoding them needed a
	// 46 KB block of SDRAM at every level start, and after a few levels the fragmented heap
	// no longer had one (out of memory). Freed below only when decoded here.
	bool panelMapped;
	unsigned char* pixels = file->loadRLEPacked(46272, panelMapped);
#else
	unsigned char* pixels = file->loadRLE(46272);
#endif


	// Create the panel background
#ifdef OJ32X
	// PORT32X: the panel surface is drawn into (ammo display), so it is a copy in SDRAM;
	// only the 32-line strip is copied, not the whole block
	panel = video.createSurface(nullptr, SW, TTOI(1));
	memcpy(panel->pixels, pixels, SW * TTOI(1));
#else
	panel = video.createSurface(pixels, SW, TTOI(1));
#endif


	// De-scramble the panel's ammo graphics
	unsigned char* sorted = new unsigned char[64 * 26];

	for (int type = 0; type < 6; type++) {
		for (int y = 0; y < 26; y++) {
			for (int x = 0; x < 64; x++)
				sorted[(y * 64) + x] = pixels[(type * 64 * 32) + (y * 64) + (x >> 2) + ((x & 3) << 4) + (55 * 320)];
		}
		panelAmmo[type] = video.createSurface(sorted, 64, 26);
	}

	delete[] sorted;
#ifdef OJ32X
	if (!panelMapped) delete[] pixels;
#else
	delete[] pixels;
#endif

	// Create the panel borders in FPS HUD mode
	panelBG[0] = video.createSurface(nullptr, TTOI(1), TTOI(1));
	panelBG[1] = video.createSurface(nullptr, TTOI(1), TTOI(1));

	constexpr int halfTile = (TTOI(1) >> 1);

	// Copy parts of the panel
	SDL_Rect src = { 176, 0, halfTile, halfTile };
	SDL_Rect dst;

	// left side gets shiny part
	dst.x = dst.y = 0; // first row
	SDL_BlitSurface(panel, &src, panelBG[0], &dst);
	dst.y = halfTile; // second row
	SDL_BlitSurface(panel, &src, panelBG[0], &dst);

	// add mirrored copy
	src.y = 0;
	src.w = 1;
	src.h = TTOI(1);
	dst.y = 0;
	for (int x = 0; x < halfTile; x++) {
		src.x = x;
		dst.x = TTOI(1) - x - 1;
		SDL_BlitSurface(panelBG[0], &src, panelBG[0], &dst);
	}

	// right side is only metal
	src.x = 199;
	src.w = 9;
	src.h = 12;
	for (int x = 0; x < TTOI(1); x += 9) {
		src.y = 0;
		dst.x = x;

		dst.y = 0; // first row
		SDL_BlitSurface(panel, &src, panelBG[1], &dst);
		src.y = 2; // second row
		dst.y = 12;
		SDL_BlitSurface(panel, &src, panelBG[1], &dst);
		src.y = 8; // third row
		dst.y = 24;
		SDL_BlitSurface(panel, &src, panelBG[1], &dst);
	}

	return E_NONE;
}


/**
 * Load a sprite.
 *
 * @param file File from which to load the sprite data
 * @param sprite Sprite that will receive the loaded data
 */
void JJ1Level::loadSprite (File* file, Sprite* sprite) {

	unsigned char* pixels;
	int pos, maskOffset;
	int width, height;

	// Load dimensions
	width = file->loadShort() << 2;
	height = file->loadShort();

	file->seek(2, false);

	maskOffset = file->loadShort();

	pos = file->loadShort() << 2;

	// Sprites can be either masked or not masked.
	if (maskOffset) {

		// Masked

		height++;

		// Skip to mask
		file->seek(maskOffset, false);

		// Find the end of the data
		pos += file->tell() + ((width >> 2) * height);

		// Read scrambled, masked pixel data
		pixels = file->loadPixels(width * height, SKEY);
		sprite->setPixels(pixels, width, height, SKEY);

		delete[] pixels;

		file->seek(pos, true);

	} else if (width) {

		// Not masked

		// Read scrambled pixel data
		pixels = file->loadPixels(width * height);
		sprite->setPixels(pixels, width, height, SKEY);

		delete[] pixels;

	}

}


/**
 * Load sprites.
 *
 * @param fileName Name of the file containing the level-specific sprites
 *
 * @return Error code
 */
int JJ1Level::loadSprites (char * fileName) {

	// Open fileName
	FilePtr specFile;
	try {

		specFile = std::make_unique<File>(fileName, PATH_TYPE_GAME);

	} catch (int e) {

		return e;

	}


	// This function loads all the sprites, not just those in fileName
	FilePtr mainFile;
	try {

		mainFile = std::make_unique<File>("MAINCHAR.000", PATH_TYPE_GAME);

	} catch (int e) {

		return e;

	}


	sprites = specFile->loadShort(256);

	// Include space in the sprite set for the blank sprite at the end
	spriteSet = new Sprite[sprites + 1];


	// Read offsets
	unsigned char* buffer = specFile->loadBlock(sprites * 2);

	for (int i = 0; i < sprites; i++)
		spriteSet[i].setOffset(buffer[i] << 2, buffer[sprites + i]);

	delete[] buffer;


	// Skip to where the sprites start in mainchar.000
	mainFile->seek(2, true);


	// Loop through all the sprites to be loaded
	for (int i = 0; i < sprites; i++) {

		bool loaded = false;

		if (mainFile->loadChar() == 0xFF) {

			// Go to the next sprite/file indicator
			mainFile->seek(1, false);

		} else {

			// Return to the start of the sprite
			mainFile->seek(-1, false);

			// Load the individual sprite data
			loadSprite(mainFile.get(), spriteSet + i);

			loaded = true;

		}

		if (specFile->loadChar() == 0xFF) {

			// Go to the next sprite/file indicator
			specFile->seek(1, false);

		} else {

			// Return to the start of the sprite
			specFile->seek(-1, false);

			// Load the individual sprite data
			loadSprite(specFile.get(), spriteSet + i);

			loaded = true;

		}

		/* If both fileName and mainchar.000 have file indicators, create a
		blank sprite */
		if (!loaded) spriteSet[i].clearPixels();


		// Check if the next sprite exists
		// If not, create blank sprites for the remainder
		if (specFile->tell() >= specFile->getSize()) {

			for (i++; i < sprites; i++) {

				spriteSet[i].clearPixels();

			}

		}

	}

	// Include a blank sprite at the end
	spriteSet[sprites].clearPixels();

	return E_NONE;

}


/**
 * Load the tileset.
 *
 * @param fileName Name of the file containing the tileset
 *
 * @return The number of tiles loaded
 */
int JJ1Level::loadTiles (char* fileName) {

	FilePtr file;

	try {

		file = std::make_unique<File>(fileName, PATH_TYPE_GAME);

	} catch (int e) {

		return e;

	}

	// Load the palette
	file->loadPalette(palette);

	// Load the background palette
	file->loadPalette(skyPalette);

	/* Skip the second, sometimes identical, background palette
	   FIXME: These are actually alternating, needs rewritten `SkyPaletteEffect` */
	file->skipRLE();

#ifdef OJ32X
	// PORT32X: the tile set comes decoded from the cart pack and is used in place. Decoding
	// it held every tile twice in SDRAM (up to 490 KB). The tiles run to the end of the file.
	const int tilesStart = file->tell();
	{
		int pw, ph;
		uint32_t end;
		const uint8_t* pic = file->packPictureAny(&pw, &ph, &end);
		if (pic && pw == TTOI(1) && (int)end == file->getSize()) {
			file->seek(end, true);
			tileSet = video.createSurface(pic, TTOI(1), ph);
			video.enableColorKey(tileSet, TKEY);
			return ph >> 5;
		}
	}
#endif

	// Load the tile pixel indices
	int tiles = 0;
	unsigned char* pixels[TSETS * TNUM] = {0};

	for (int i = 0; i < TSETS; i++) {
		// Check if this tileset is enabled
		char * marker = file->loadString(2);
		if(strncmp(marker, "ok", 2) == 0) {
			for(int j = 0; j < TNUM; j++) {
				// Read the RLE pixels
				pixels[i * TNUM + j] = file->loadRLE(TTOI(1) * TTOI(1));
			}
			tiles += TNUM;
			LOG_MAX("Loaded tileset %d", i);
		} else if (strncmp(marker, "  ", 2) == 0) { // Empty tilesets have marker of 2 spaces
			LOG_TRACE("Skipping empty tileset %d", i);
		}
		delete[] marker;
	}

	if (file->getSize() != file->tell()) {
		LOG_WARN("Tileset data is corrupted");

		for (int i = 0; i < tiles; i++)
			delete[] pixels[i];

		return E_FILE;
	}

	LOG_TRACE("Loaded %d tiles", tiles);

	// Create combined buffer
	unsigned char* buffer = new unsigned char[TTOI(1) * TTOI(tiles)];
	for (int i = 0; i < tiles; i++) {
		memcpy(buffer + TTOI(1) * TTOI(1) * i,
			pixels[i], TTOI(1) * TTOI(1));
		delete[] pixels[i];
	}

#if defined(OJ32X) && !defined(OJ32X_MARS)
	if (::getenv("T32X_TILESTAT")) {
		int n = 0, tot = 0, tl = 0;
		for (int i = 0; i < tiles; i++) {
			int c = 0;
			for (int k = 0; k < TTOI(1) * TTOI(1); k++) {
				unsigned char v = buffer[TTOI(1) * TTOI(1) * i + k];
				if (v != TKEY) tot++;
				if (v >= 156 && v != TKEY) c++;
			}
			if (c) { n += c; tl++; if (::getenv("T32X_TILESTAT")[0] == '2') fprintf(stderr, "tile %d: %d sky-range px\n", i, c); }
		}
		fprintf(stderr, "TILESTAT %s: %d tiles, %d with sky-range pixels, %d of %d px, TKEY %d\n", fileName, tiles, tl, n, tot, TKEY);
	}
#endif
#ifdef OJ32X
	// PORT32X: the PC build records the decoded tile set for the pack (T32X_BAKE)
	file->bakePicture(tilesStart, buffer, TTOI(1), TTOI(tiles));
#endif
	tileSet = video.createSurface(buffer, TTOI(1), TTOI(tiles));
	video.enableColorKey(tileSet, TKEY);
	delete[] buffer;

#if OJ_DEBUG
	// create an empty tileset for showing the mask
	maskedTileset = video.createSurface(nullptr, TTOI(1), TTOI(tiles));
	video.enableColorKey(maskedTileset, TKEY);
#endif

	return tiles;
}


/**
 * Load the level.
 *
 * @param fileName Name of the file containing the level data
 * @param checkpoint Whether or not the player(s) will start at a checkpoint
 *
 * @return Error code
 */
int JJ1Level::load (char* fileName, bool checkpoint) {
	unsigned char* buffer;

#ifdef OJ32X
	// PORT32X: bring this level's world pack (both levels of the planet, its tiles, sprites
	// and planet screen) into the cart. Nothing is loaded when the pack is already there,
	// e.g. for a planet's second level; otherwise the 32X shows a loading screen.
	oj32x_level_pack(fileName);
#endif
#if defined(OJ32X) && !defined(__sh__)
	{ extern unsigned int oj32x_sprite_hist[256]; memset(oj32x_sprite_hist, 0, sizeof(oj32x_sprite_hist)); }
#endif

	// Load font
	try {

		font = new Font(false);

	} catch (int e) {

		return e;

	}
	#if DEBUG_FONTS
	font->saveAtlasAsBMP("levelfont.bmp");
	#endif

	// Load panel
	int res = loadPanel();
	if (res < 0) {
		delete font;

		return res;
	}


	// Show loading screen

	// Open planet.### file
	char* string = nullptr;
	if (!strcmp(fileName, LEVEL_FILE)) {

		// Using the downloaded level file

		string = createString("DOWNLOADED");

	} else {

		// Load the planet's name from the planet.### file

		FilePtr planetFile;
		string = createFileName("PLANET", fileName + strlen(fileName) - 3);

		try {

			planetFile = std::make_unique<File>(string, PATH_TYPE_GAME);

		} catch (int e) {

			planetFile = nullptr;

		}

		delete[] string;

		if (planetFile) {

			planetFile->seek(2, true);
			string = planetFile->loadTerminatedString();

		} else {

			string = createString("CUSTOM");

		}

	}

	char* levelname = new char[strlen(string) + 14];
	strcpy(levelname, string);
	delete[] string;

	switch (fileName[5]) {
		case '0':
			strcat(levelname, " LEVEL ONE");
			break;

		case '1':
			strcat(levelname, " LEVEL TWO");
			break;

		case '2':
			strcat(levelname, " SECRET LEVEL");
			break;

		default:
			strcat(levelname, " LEVEL");
			break;
	}

	video.setPalette(menuPalette);
	video.clearScreen(0);

	const char *loadingString = "LOADING ";
	int stringWidth = fontmn2->getStringWidth(loadingString) + fontmn2->getStringWidth(levelname);
	Point pos = fontmn2->showString(loadingString, (canvasW - stringWidth) >> 1, canvasH >> 1);
	fontmn2->showString(levelname, pos.x, canvasH >> 1);

	camelcaseString(levelname);
	video.setTitle(levelname);
	delete[] levelname;

	if (::loop(NORMAL_LOOP) == E_QUIT) return E_QUIT;


	// Open level file
	FilePtr file;
	try {

		if (!strcmp(fileName, LEVEL_FILE))
			// use downloaded file
			file = std::make_unique<File>(fileName, PATH_TYPE_TEMP);
		else
			file = std::make_unique<File>(fileName, PATH_TYPE_GAME);

	} catch (int e) {

		deletePanel();
		delete font;

		return e;

	}

	// Checking level file header
	char *identifier1 = file->loadString(2);
	char identifier2 = file->loadChar();
	if (strncmp(identifier1, "DD", 2) != 0 || identifier2 != 0x1A) {
		LOG_ERROR("Level not valid!");
		delete[] identifier1;
		return E_FILE;
	}
	delete[] identifier1;

	// Load the blocks.### extension

	// Skip past all level data
	file->seek(39, true);
	file->skipRLE();
	file->skipRLE();
	file->skipRLE();
	file->skipRLE();
	file->skipRLE();
	file->skipRLE();
	file->skipRLE();
	file->skipRLE();
	file->seek(598, false);
	file->skipRLE();
	file->seek(4, false);
	file->skipRLE();
	file->skipRLE();
	file->seek(25, false);
	file->skipRLE();
	file->seek(3, false);

	// Load the level number
	levelNum = file->loadChar() ^ 210;

	// Load the world number
	worldNum = file->loadChar() ^ 4;

	// Load 100% counters
	nEnemies[0] = file->loadShort(); // Easy
	nEnemies[1] = nEnemies[0]; // Medium is same as Easy
	nEnemies[2] = file->loadShort(); // Hard
	nEnemies[3] = file->loadShort(); // Turbo
	nItems = file->loadShort();

	// Load tile set from appropriate blocks.###

	// Load tile set extension
	char *ext = file->loadTerminatedString(3);

	// Create tile set file name
	if (!strcmp(ext, "999")) string = createFileName("BLOCKS", worldNum);
	else string = createFileName("BLOCKS", ext);

	delete[] ext;

#ifdef OJ32X
	// PORT32X: memory measuring points - logged (the 32X's last log lines; on the PC build
	// tools/heap_peak.sh groups the heap by them)
	oj32x_mem_mark("level: before tiles");
#endif
	int tiles = loadTiles(string);
#ifdef OJ32X
	oj32x_mem_mark("level: tiles loaded");
#endif

	delete[] string;

	if (tiles < 0) {

		deletePanel();
		delete font;

		return tiles;

	}


	// Load sprite set from corresponding Sprites.###

	string = createFileName("SPRITES", worldNum);
	res = loadSprites(string);
#ifdef OJ32X
	oj32x_mem_mark("level: sprites loaded");
#endif

	delete[] string;

	if (res < 0) {
		video.destroySurface(tileSet);
#if OJ_DEBUG
		video.destroySurface(maskedTileset);
#endif
		deletePanel();
		delete font;

		return res;
	}


	// Skip to tile and event reference data
	file->seek(39, true);

	// Load tile and event references

#ifdef OJ32X
	// PORT32X: the decoded tile and event references in place from the cart pack (no 32 KB
	// buffer in SDRAM); the grid is built from them as upstream does
	const int gridStart = file->tell();
	bool bakedGrid = false;
	{
		int gw, gh;
		uint32_t gend;
		const unsigned char* baked = file->packPictureAny(&gw, &gh, &gend);
		if (baked && gw * gh == LW * LH * 2) {
			file->seek(gend, true);
			buffer = const_cast<unsigned char*>(baked);
			bakedGrid = true;
		}
	}
	if (!bakedGrid)
#endif
	buffer = file->loadRLE(LW * LH * 2);

#ifdef OJ32X
	nEventTimes = 0;  // PORT32X: empty event time table (see jj1level.h)
#endif
	// Create grid from data
	for (int x = 0; x < LW; x++) {

		for (int y = 0; y < LH; y++) {

			grid[y][x].tile = buffer[(y + (x * LH)) << 1];
			grid[y][x].bg = buffer[((y + (x * LH)) << 1) + 1] >> 7;
			grid[y][x].event = buffer[((y + (x * LH)) << 1) + 1] & 127;
			grid[y][x].hits = 0;
#ifndef OJ32X
			grid[y][x].time = 0;
#endif

		}

	}

#ifdef OJ32X
	if (!bakedGrid) {
		file->bakePicture(gridStart, buffer, LW * 2, LH);  // PORT32X: PC build records it
		delete[] buffer;
	}
#else
	delete[] buffer;
#endif

	// Ignore tile transparency settings, these are applied based on event type/behaviour

	file->skipRLE();


	// Load mask data

	buffer = file->loadRLE(MASKS); // TODO: find out how the 16 extra masks are used

	// Unpack bits
	for (int i = 0; i < tiles; i++) {
		for (int y = 0; y < 8; y++) {
			for (int x = 0; x < 8; x++)
				mask[i][(y << 3) + x] = (buffer[(i << 3) + y] >> x) & 1;
		}
	}

	delete[] buffer;

#if OJ_DEBUG
	// This let's us see the mask of the tile graphics during gameplay

	if (SDL_MUSTLOCK(maskedTileset)) SDL_LockSurface(maskedTileset);

	for (int i = 0; i < tiles; i++) {
		for (int y = 0; y < 32; y++) {
			for (int x = 0; x < 32; x++) {

				bool masked = (mask[i][((y >> 2) << 3) + (x >> 2)] == 1);
				((char *)(maskedTileset->pixels))[(i * 1024) + (y * 32) + x] =
					masked ? 88 : TKEY; // orange

			}
		}
	}

	if (SDL_MUSTLOCK(maskedTileset)) SDL_UnlockSurface(maskedTileset);
#endif

	// Load special event path
	buffer = file->loadRLE(PATHS << 9);

	for (int type = 0; type < PATHS; type++) {

		path[type].length = buffer[type << 9] + (buffer[(type << 9) + 1] << 8);
		if (path[type].length < 1) path[type].length = 1;
		path[type].x = new short int[path[type].length];
		path[type].y = new short int[path[type].length];

		for (int i = 0; i < path[type].length; i++) {

			path[type].x[i] = reinterpret_cast<signed char*>(buffer)[(type << 9) + (i << 1) + 3] << 2;
			path[type].y[i] = reinterpret_cast<signed char*>(buffer)[(type << 9) + (i << 1) + 2];

		}

	}

	delete[] buffer;


	// Load event set
	buffer = file->loadRLE(EVENTS * ELENGTH);

	// Fill event set with data
	for (int i = 0; i < EVENTS; i++) {

		eventSet[i].difficulty           = static_cast<difficultyType>(buffer[i* ELENGTH]);
		eventSet[i].reflection           = buffer[(i* ELENGTH) + 2];
		eventSet[i].movement             = buffer[(i* ELENGTH) + 4];
		eventSet[i].anims[E_LEFTANIM]    = buffer[(i* ELENGTH) + 5];
		eventSet[i].anims[E_RIGHTANIM]   = buffer[(i* ELENGTH) + 6];
		eventSet[i].magnitude            = buffer[(i* ELENGTH) + 8];
		eventSet[i].strength             = buffer[(i* ELENGTH) + 9];
		eventSet[i].modifier             = buffer[(i* ELENGTH) + 10];
		eventSet[i].points               = buffer[(i* ELENGTH) + 11];
		eventSet[i].bullet               = buffer[(i* ELENGTH) + 12];
		eventSet[i].bulletPeriod         = buffer[(i* ELENGTH) + 13];
		eventSet[i].speed                = buffer[(i* ELENGTH) + 15] + 1;
		eventSet[i].animSpeed            = buffer[(i* ELENGTH) + 17] + 1;
		auto se = static_cast<SE::Type>(buffer[(i * ELENGTH) + 21]);
		if (!isValidSoundIndex(se)) {
			eventSet[i].sound = SE::NONE;
			LOG_WARN("Event %d has invalid sound effect %d.", i, se);
		} else {
			eventSet[i].sound = se;
		}
		eventSet[i].multiA               = buffer[(i * ELENGTH) + 22];
		eventSet[i].multiB               = buffer[(i * ELENGTH) + 23];
		eventSet[i].pieceSize            = buffer[(i * ELENGTH) + 24];
		eventSet[i].pieces               = buffer[(i * ELENGTH) + 25];
		eventSet[i].angle                = buffer[(i * ELENGTH) + 26];
		eventSet[i].anims[E_LFINISHANIM] = buffer[(i * ELENGTH) + 28];
		eventSet[i].anims[E_RFINISHANIM] = buffer[(i * ELENGTH) + 29];
		eventSet[i].anims[E_LSHOOTANIM]  = buffer[(i * ELENGTH) + 30];
		eventSet[i].anims[E_RSHOOTANIM]  = buffer[(i * ELENGTH) + 31];

	}

	// Process grid

	enemies = items = 0;

	for (int x = 0; x < LW; x++) {
		for (int y = 0; y < LH; y++) {

			int type = grid[y][x].event;
			if (type) {
				// If the event hurts and can be killed, it is an enemy
				// Anything else that scores is an item
				if ((eventSet[type].modifier == 0) && eventSet[type].strength) enemies++;
				else if (eventSet[type].points) items++;
			}
		}
	}

	delete[] buffer;

#if DEBUG_LOAD
	// Show event names
	buffer = file->loadRLE(EVENTS * LONGNAME);

	for (int i = 0; i < EVENTS; i++) {
		char displayName[LONGNAME] = {0};
		strncpy(displayName, reinterpret_cast<char *>(buffer + i * LONGNAME + 1), buffer[i * LONGNAME]);
		displayName[LONGNAME-1] = '\0';

		if (strlen(displayName)) {
			LOG_MAX("Event id %d is named \"%s\"", i, displayName);
		}
	}
	delete[] buffer;
#else
	// Skip (usually empty) event names
	file->skipRLE();
#endif

	// Load animation set

	buffer = file->loadRLE(ANIMS << 6);

	// Create animation set based on that data
	for (int i = 0; i < ANIMS; i++) {

		animSet[i].setData(buffer[(i << 6) + 6],
			buffer[i << 6], buffer[(i << 6) + 1],
			buffer[(i << 6) + 3], buffer[(i << 6) + 4],
			buffer[(i << 6) + 2], buffer[(i << 6) + 5]);

		for (int y = 0; y < buffer[(i << 6) + 6]; y++) {

			// Get frame
			int x = buffer[(i << 6) + 7 + y];
			if (x > sprites) x = sprites;

			// Assign sprite and vertical offset
			animSet[i].setFrame(y, true);
			animSet[i].setFrameData(spriteSet + x,
				buffer[(i << 6) + 26 + y], buffer[(i << 6) + 45 + y]);

		}

	}

	delete[] buffer;

#if DEBUG_LOAD
	// Show animation names
	buffer = file->loadRLE(ANIMS * LONGNAME);

	for (int i = 0; i < ANIMS; i++) {
		char displayName[LONGNAME] = {0};
		strncpy(displayName, reinterpret_cast<char *>(buffer + i * LONGNAME + 1), buffer[i * LONGNAME]);
		displayName[LONGNAME-1] = '\0';

		if (strlen(displayName)) {
			LOG_MAX("Animation id %d is named \"%s\"", i, displayName);
		}
	}
	delete[] buffer;

	// Show level block names
	for (int i = 0; i < 16; i++) {
		char *tmpName = file->loadTerminatedString(SHORTNAME);
		if (strlen(tmpName)) {
			LOG_MAX("Level block id %d is named \"%s\"", i, tmpName);
		}
		delete[] tmpName;
	}

	// Skip compression info
	file->seek(9);
#else
	// Skip (usually empty) animation names
	file->skipRLE();

	// Skip level block names and compression info
	file->seek(16 * (SHORTNAME + 1) + 9);
#endif

	// Load sound map
	unsigned short int soundRates[SOUNDS];
	for (int i = 0; i < SOUNDS; i++) {
		soundRates[i] = file->loadShort();
	}
	for (int i = 0; i < SOUNDS; i++) {
		char *tmpName = file->loadTerminatedString(SHORTNAME);
		resampleSound(i, tmpName, soundRates[i]);
		delete[] tmpName;
	}

	// Music file
	musicFile = file->loadTerminatedString(12);
#if DEBUG_LOAD
	if (strlen(musicFile)) {
		LOG_MAX("Music is \"%s\"", musicFile);
	}
#endif

	// Skip (usually empty) level start cutscene
	file->seek(13);

	// End of episode cutscene
	sceneFile = file->loadTerminatedString(12);
#if DEBUG_LOAD
	if (strlen(sceneFile)) {
		LOG_MAX("End scene is \"%s\"", sceneFile);
	}
#endif

	// Skip level editor tileset files
	file->seek(39);

	// The players' initial coordinates
	unsigned char startX = file->loadShort(LW);
	unsigned char startY = file->loadShort(LH) + 1;

	// Next level
	int l = file->loadChar();
	int w = file->loadChar();
	setNext(l, w);

	// jump height
	jumpHeight = (file->loadShort() - 0xFFFF) / 2;
	if (jumpHeight != -5)
		LOG_TRACE("Uncommon jumpHeight: %i", jumpHeight);

	// skip some unknown level
	file->seek(2);

	// Thanks to Doubble Dutch for the water level bytes
	waterLevelTarget = ITOF(file->loadShort() + 17);
	waterLevel = waterLevelTarget - F8;
	waterLevelSpeed = -80000;

	// Jazz animation speed
	animSpeed = file->loadChar();
	if (animSpeed != 119)
		LOG_TRACE("Uncommon animationSpeed: %i", animSpeed);

	// Skip an unknown value (end marker?)
	file->seek(2);


	// Thanks to Feline and the JCS94 team for the next bits:

	// Load player's animation set references (always left + right)
	Anim* pAnims[JJ1PANIMS];
	buffer = file->loadRLE(JJ1PANIMS * 2);
	string = new char[MTL_P_ANIMS + JJ1PANIMS];

	for (int i = 0; i < JJ1PANIMS; i++) {
		playerAnims[i] = buffer[i << 1];
		pAnims[i] = animSet + playerAnims[i];
		string[MTL_P_ANIMS + i] = playerAnims[i];
	}

	delete[] buffer;

	if (multiplayer) {

		string[0] = MTL_P_ANIMS + JJ1PANIMS;
		string[1] = MT_P_ANIMS;
		string[2] = 0;
		game->send(reinterpret_cast<unsigned char*>(string));

	}

	delete[] string;


	createLevelPlayers(LT_JJ1, pAnims, NULL, checkpoint, startX, startY);


	// Load miscellaneous animations
	for (int i = 0; i < JJ1MANIMS; i++) {
		miscAnims[i] = file->loadChar();
	}


	// Load bullet set
	buffer = file->loadRLE(BULLETS * BLENGTH);

	for (int i = 0; i < BULLETS; i++) {

		memcpy(bulletSet[i], buffer + (i * BLENGTH), BLENGTH);

	}

	delete[] buffer;

#if DEBUG_LOAD
	// Show attack names
	buffer = file->loadRLE(BULLETS * 21);

	for (int i = 0; i < BULLETS; i++) {
		char displayName[20] = {0};
		strncpy(displayName, reinterpret_cast<char *>(buffer + i * 21 + 1), buffer[i * 21]);
		displayName[20-1] = '\0';

		if (strlen(displayName)) {
			LOG_MAX("Attack id %d is named \"%s\"", i, displayName);
		}
	}
	delete[] buffer;
#else
	// Skip (usually empty) attack names
	file->skipRLE();
#endif

	// Load level properties (magic)

	// First byte is the background palette effect type
	int type = file->loadChar();
	sky = false;

	switch (type) {
		case PE_SKY:
			sky = true;

			// Sky background effect
			paletteEffects = new SkyPaletteEffect(156, 100, FH, skyPalette, NULL);

			break;

		case PE_2D:
			// Parallaxing background effect
			paletteEffects = new P2DPaletteEffect(128, 64, FE, NULL);

			break;

		case PE_1D:
			// Diagonal stripes "parallaxing" background effect
			paletteEffects = new P1DPaletteEffect(128, 32, FH, NULL);

			break;

		case PE_WATER:
			// The deeper below water, the darker it gets
			paletteEffects = new WaterPaletteEffect(TTOF(32), NULL);

			break;

		default:
			// No effect
			paletteEffects = NULL;
			LOG_TRACE("Unknown palette effect: %d", type);

			break;
	}

	// Palette animations
	// These are applied to every level without a conflicting background effect
	// As a result, there are a few levels with things animated that shouldn't
	// be

	// In Diamondus: The red/yellow palette animation
	paletteEffects = new RotatePaletteEffect(112, 4, F32, paletteEffects);

	// In Diamondus: The waterfall palette animation
	paletteEffects = new RotatePaletteEffect(116, 8, F16, paletteEffects);

	// The following were discoverd by Unknown/Violet

	paletteEffects = new RotatePaletteEffect(124, 3, F16, paletteEffects);

	if ((type != PE_1D) && (type != PE_2D))
		paletteEffects = new RotatePaletteEffect(132, 8, F16, paletteEffects);

	if ((type != PE_SKY) && (type != PE_2D))
		paletteEffects = new RotatePaletteEffect(160, 32, -F16, paletteEffects);

	if (type != PE_SKY) {

		paletteEffects = new RotatePaletteEffect(192, 32, -F32, paletteEffects);
		paletteEffects = new RotatePaletteEffect(224, 16, F16, paletteEffects);

	}

	// Level fade-in/white-in effect
	if (checkpoint) paletteEffects = new FadeInPaletteEffect(T_START, paletteEffects);
	else paletteEffects = new WhiteInPaletteEffect(T_START, paletteEffects);


	// Check if a sun/star/distant planet, etc. is visible
	skyOrb = file->loadChar();

	// If so, find out which tile it uses or skip it
	if (skyOrb) skyOrb = file->loadChar();
	else file->loadChar();

	// Load level sound effects
	for (int i = 0; i < JJ1LSOUNDS; i++) {
		levelSounds[i] = file->loadChar();
#if 1//DEBUG_LOAD
		if (i == LSND_NOTHING1 || i == LSND_UNKNOWN6 || i == LSND_UNKNOWN9) {
			if (levelSounds[i])
				LOG_MAX("Level Sound %d is %d", i, levelSounds[i]);
		}
#endif
	}

	// Load level animations (shield gems, board, bird, shiver/slide)
	for (int i = 0; i < JJ1LANIMS; i++) {
		levelAnims[i] = file->loadChar();
#if 1//DEBUG_LOAD
		if (i == LA_NOTHING1 || i == LA_NOTHING2 || i == LA_NOTHING3) {
			if (levelAnims[i])
				LOG_MAX("Level Animation %d is %d", i, levelAnims[i]);
		}
#endif
	}
	if (levelAnims[LA_UNKNOWN9] != 31)
		LOG_TRACE("Uncommon level animation 9: %i", levelAnims[LA_UNKNOWN9]);

	// And that's us done!


	// Set the tick at which the level will end
	endTime = (5 - +getDifficulty()) * 2 * 60 * 1000;


	events = nullptr;
	bullets = nullptr;

	energyBar = 0;
	ammoType = 0;
	ammoOffset = -1;

#ifdef OJ32X
	oj32x_mem_mark("level: loaded");
#endif
#ifdef OJ32X
	// REV: the Genesis plane draws this level's background tiles if its world pack has the
	// file for it (tools/genesis_convert.py; port/genbg.h)
	{
		unsigned char rgb[768];
		for (int i = 0; i < 256; i++) { rgb[i * 3] = palette[i].r; rgb[i * 3 + 1] = palette[i].g; rgb[i * 3 + 2] = palette[i].b; }
		genbg_level_loaded(levelNum, sky, rgb);
	}
#endif
#if defined(OJ32X) && !defined(__sh__)
	// REV: T32X_DUMP_GB=<folder> - the level as the Genesis converter (tools/genesis_convert.py)
	// reads it: GB<world>_<level>.DMP = "OJGD", effect type, sky, sky orb, tiles, palette
	// (256 x RGB), sky palette (256 x RGB), tile pixels (tiles x 32 x 32), grid (LH x LW x
	// {tile, flags: 1 black cell, 2 foreground tile (drawn in front of the sprites)})
	if (const char* dir = getenv("T32X_DUMP_GB")) {
		char path[512];
		snprintf(path, sizeof(path), "%s/GB%03d_%d.DMP", dir, worldNum, levelNum);
		if (FILE* f = fopen(path, "wb")) {
			const int nt = tileSet->h / TTOI(1);
			unsigned char head[8] = { 'O', 'J', 'G', 'D', (unsigned char)type, (unsigned char)sky,
				skyOrb, (unsigned char)nt };
			fwrite(head, 1, 8, f);
			for (int i = 0; i < 256; i++) { fputc(palette[i].r, f); fputc(palette[i].g, f); fputc(palette[i].b, f); }
			for (int i = 0; i < 256; i++) { fputc(skyPalette[i].r, f); fputc(skyPalette[i].g, f); fputc(skyPalette[i].b, f); }
			for (int y = 0; y < nt * TTOI(1); y++)
				fwrite(static_cast<unsigned char*>(tileSet->pixels) + y * tileSet->pitch, 1, TTOI(1), f);
			for (int y = 0; y < LH; y++)
				for (int x = 0; x < LW; x++) {
					const GridElement* ge = &grid[y][x];
					const bool fg = (ge->event == 123) || (ge->event == 124) || (ge->event == 125) ||
						(eventSet[ge->event].movement == 37) || (eventSet[ge->event].movement == 38);
					fputc(ge->tile, f);
					fputc((ge->bg ? 1 : 0) | (fg ? 2 : 0), f);
				}
			// the palette indices the sprites use (counted while they were loaded)
			extern unsigned int oj32x_sprite_hist[256];
			for (int i = 0; i < 256; i++) {
				const unsigned int n = oj32x_sprite_hist[i];
				fputc(n >> 24, f); fputc(n >> 16, f); fputc(n >> 8, f); fputc(n, f);
			}
			fclose(f);
		}
	}
#endif
	return E_NONE;

}
