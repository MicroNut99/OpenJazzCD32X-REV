
/**
 *
 * @file jj1bonuslevel.h
 *
 * Part of the OpenJazz project
 *
 * @par History:
 * - 3rd February 2009: Created bonus.h
 * - 1st August 2012: Renamed bonus.h to jj1bonuslevel.h
 *
 * @par Licence:
 * Copyright (c) 2009-2017 AJ Thomson
 *
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 */


#ifndef _BONUS_H
#define _BONUS_H

#include "io/gfx/anim.h"
#include "level/level.h"


// Constants

// General
#define BLW    256 /* Bonus level width */
#define BLH    256 /* Bonus level height */
#define BANIMS  32
#define BEVENTS 16
#define BSOUNDS 16

#define T_BONUS_END 2000


// Datatype

/// JJ1 bonus level grid element
typedef struct {

	unsigned char tile;  ///< Indexes the tile set
	unsigned char event; ///< Event type

} JJ1BonusLevelGridElement;

/// JJ1 bonus level event type
typedef struct {

	unsigned char anim; ///< Index of animation
	unsigned char type; ///< Type: time, gem, pole-bounce, exit, hand-bounce
	unsigned char passable; ///< Whether Jazz bounces back
	unsigned char used; ///< Whether this is a real event
	SE::Type      sound; ///< The sound played on the appropriate trigger

} JJ1BonusEventType;

// Classes

class Font;

/// JJ1 bonus level
class JJ1BonusLevel : public Level {

	private:
		SDL_Surface*             tileSet; ///< Tile images
		SDL_Surface*             background; ///< Background image
		Font*                    font; ///< On-screen message font
		Sprite*                  spriteSet; ///< Sprite images
		Anim                     animSet[BANIMS]; ///< Animations
		JJ1BonusEventType        eventSet[BEVENTS]; ///< Event types
#ifdef OJ32X
		// PORT32X: the 256 x 256 map is not copied into SDRAM (upstream's grid is 128 KB, more
		// than the 32X has free). Its tile and event planes are read in place from the bonus
		// level's cart pack, where the PC build stored them decoded; collected items are
		// marked in an 8 KB bit table instead of being cleared from the map. tileAt() and
		// eventAt() replace every read of grid[y][x], clearEvent() every write of 0 to an event.
		// On the PC build, without a mounted pack, the planes are decoded into the heap
		// (tilesMapped / eventsMapped = false) and freed with the level.
		const unsigned char* mapTiles;
		const unsigned char* mapEvents;
		bool tilesMapped, eventsMapped;
		unsigned char clearedEvents[BLW * BLH / 8];
		int mdHole; ///< R9: the floor is the Genesis's (the 32X fills its area with this index), -1: the 32X's
		class RotatePaletteEffect* mdRot; ///< R9: the track sides' rotation (its step picks the floor's stamps)
	public:
		unsigned char tileAt (int y, int x) const { unsigned char t = mapTiles[(y << 8) + x]; return t > 59 ? 59 : t; }
		unsigned char eventAt (int y, int x) const {
			const int i = (y << 8) + x;
			return (clearedEvents[i >> 3] & (1 << (i & 7))) ? 0 : mapEvents[i];
		}
		void clearEvent (int y, int x) { const int i = (y << 8) + x; clearedEvents[i >> 3] |= (unsigned char)(1 << (i & 7)); }
	private:
#else
		JJ1BonusLevelGridElement grid[BLH][BLW]; ///< Level grid
#endif
		char                     mask[60][64]; ///< Tile masks (at most 60 tiles, all with 8 * 8 masks)
		fixed                    direction; ///< Player's direction

		JJ1BonusLevel(const JJ1BonusLevel&); // non construction-copyable
		JJ1BonusLevel& operator=(const JJ1BonusLevel&); // non copyable

		int  loadSprites ();
		int  loadTiles   (char* fileName);
		bool isEvent     (fixed x, fixed y);
		int  step        ();
		void draw        ();

	public:
		JJ1BonusLevel  (Game* owner, char* fileName, bool multi);
		~JJ1BonusLevel () override;

		bool checkMask (fixed x, fixed y);
		void receive   (unsigned char* buffer) override;
		int  play      ();

};

#endif

