/**
 * @file credits32x.cpp
 *
 * PORT32X: the credits screen with a CD player, opened by the main menu's last entry in
 * place of "Exit game" (a console has nothing to exit to). A plasma background, the credits
 * in the game's own font (two pages), and below them the CD player for the soundtrack: the
 * game's songs as CD audio tracks 2-41 (tools/music_tracks.txt), named after the planets
 * that play them.
 *
 * Controls:
 *   Left / Right       move the selector (nothing else changes; after 5 s without a choice
 *                      it returns to the playing track)
 *   A, C or Start      play the selected track (on the playing track: stop / play)
 *   Y, X, Z, Mode,     hide / show the credits text (Up / Down for 3-button pads)
 *   Up or Down
 *   B                  back to the main menu
 * Each track plays once, then the next track on the disc, after the last the first again.
 */

#ifdef OJ32X

#include "credits32x.h"
#include "plasma.h"

#include "io/controls.h"
#include "io/gfx/font.h"
#include "io/gfx/video.h"
#include "io/sound.h"
#include "oj32x.h"
#include "loop.h"

#include <stdio.h>
#include "music_lengths.h"
#include <stdlib.h>

namespace {

	struct Song { const char* file; const char* name; };

	// Disc tracks 2-41 in this order (tools/music_tracks.txt): the game's songs
	// alphabetically, then Holiday Hare '95's own. The names say which planet's levels play
	// each song (read from the game's level files).
	const Song songs[] = {
		{ "BONUS",    "BONUS STAGE" },
		{ "BOSS",     "BOSS - GUARDIAN" },
		{ "ENDLEVEL", "END OF LEVEL" },
		{ "ENDSONG",  "ENDING" },
		{ "MENUSNG",  "MAIN MENU" },
		{ "SECRET",   "SECRET" },
		{ "SONG0",    "DIAMONDUS" },
		{ "SONG1",    "SLUGGION" },
		{ "SONG10",   "CRYSILIS" },
		{ "SONG11",   "ORBITUS" },
		{ "SONG12",   "PEZROCK" },
		{ "SONG13",   "DREEMPIPES" },
		{ "SONG14",   "SCRAPARAP" },
		{ "SONG15",   "BATTLESHIPS" },
		{ "SONG16",   "MEGAIRBASE" },
		{ "SONG17",   "MEDIVO" },
		{ "SONG2",    "TECHNOIR" },
		{ "SONG3",    "TUBELECTRIC" },
		{ "SONG4",    "TURTEMPLE" },
		{ "SONG5",    "NIPPIUS" },
		{ "SONG6",    "JUNGROCK" },
		{ "SONG7",    "LETNI" },
		{ "SONG8",    "FANOLINT" },
		{ "SONG9",    "MARBELARA" },
		{ "SONGCD0",  "EXOTICUS" },
		{ "SONGCD1",  "INDUSTRIUS" },
		{ "SONGCD2",  "MUCKAMOK" },
		{ "SONGCD3",  "RANEFORUS" },
		{ "SONGCD4",  "STONAR" },
		{ "SONGCD5",  "DECKSTAR" },
		{ "SONGCD6",  "CERAMICUS" },
		{ "SONGCD7",  "DESERTO" },
		{ "SONGCD8",  "LAGUNICUS" },
		{ "XM2",      "HOLIDAIUS" },
		{ "XM3",      "HOLIDAIUS 2" },
		{ "XMAS2",    "CANDION" },
		{ "XMAS3",    "BLOXONIUS" },
		{ "H95MENU",  "HOLIDAY HARE 95 MENU" },
		{ "H95ENDLV", "HOLIDAY HARE 95 LEVEL END" },
		{ "H95XM3",   "HOLIDAY HARE 95 STAGE 3" }
	};
	const int nSongs = sizeof(songs) / sizeof(songs[0]);
	const int firstSong = 29;   // the player starts with track 31, SONGCD5 (Deckstar)

	// Two pages, each shown for 8 seconds
	const char* const page1[] = {
		"JAZZ JACKRABBIT",
		"EPIC MEGAGAMES 1994",
		"",
		"A GAME BY",
		"ARJAN BRUSSEE",
		"CLIFF BLESZINSKI",
		"",
		"OPENJAZZ",
		"ALISTER THOMSON",
		"AND CONTRIBUTORS"
	};
	const char* const page2[] = {
		"D32XR 32X CODE",
		"VICTOR LUCHITS",
		"",
		"SEGA CD AND 32X FRAMEWORK",
		"CHILLY WILLY",
		"",
		"SEGA CD32X PORT",
		"MICRONUT99",
		"",
		"THANK YOU FOR PLAYING!"
	};

}

int runCredits32X () {

	Plasma plasma;
	// the font: the big font; on the PC T32X_CREDITS_FONT=0-4 picks another (for comparison)
	Font* fonts[] = { fontmn2, fontbig, font2, fontmn1, fontiny };
	const char* fe = getenv("T32X_CREDITS_FONT");
	Font* font = fonts[fe ? atoi(fe) % 5 : 1];
	int song = firstSong;      // the track that plays
	int pick = firstSong;      // the track the selector points at (Left/Right)
	unsigned int pickTime = 0; // when the selector last moved (0 = not moved)
	bool playing = true;
	bool showText = true, yWas = false;
	char line[64];

	// each track plays once, then the next one (the lengths come from the WAVs:
	// port/music_lengths.h, tools/music_lengths.py); unknown length = the track repeats
	auto play = [&](int s) {
		const unsigned int ms = music_length_ms[s + 2];
		oj32x_music_track(ms ? ((s + 2) | 0x8000) : (s + 2));
		return globalTicks + (ms ? ms + 2000 : 0);
	};
	unsigned int ends = play(song);
	unsigned int start = globalTicks;

	while (true) {

		if (loop(NORMAL_LOOP) == E_QUIT) return E_QUIT;

		if (controls.release(C_ESCAPE)) break;

		// Y shows / hides the credits text (the background and player stay)
		{
			// Y, X, Z or Mode (the 6-button pad's extra buttons: 0x0F00)
			const bool y = (oj32x_pad() & 0x0F00) != 0;
			if (y && !yWas) showText = !showText;
			yWas = y;
			// Up / Down do the same (a 3-button pad has no Y)
			if (controls.release(C_UP) || controls.release(C_DOWN)) showText = !showText;
		}

		// Left/Right only move the selector; A/C/Start plays the choice. Without a choice
		// nothing changes, and after 5 s the selector goes back to the playing track.
		if (controls.release(C_LEFT)) {
			pick = (pick + nSongs - 1) % nSongs;
			pickTime = globalTicks;
		}
		if (controls.release(C_RIGHT)) {
			pick = (pick + 1) % nSongs;
			pickTime = globalTicks;
		}
		if (pickTime && globalTicks - pickTime >= 5000) {
			pick = song;
			pickTime = 0;
		}
		if (controls.release(C_ENTER)) {
			if (pick != song || !playing) {
				song = pick;
				playing = true;
				ends = play(song);
			} else {
				playing = false;           // A on the track that plays: stop
				oj32x_music_track(0);
			}
			pickTime = 0;
		}
		// the end of a track: the next one
		if (playing && music_length_ms[song + 2] && (int)(globalTicks - ends) >= 0) {
			// on to the next track that is on the disc - a song whose WAV was missing at build
			// time has no length and is skipped; after the last, the first again
			for (int n = 0; n < nSongs; n++) {
				song = (song + 1) % nSongs;
				if (music_length_ms[song + 2]) break;
			}
			if (!pickTime) pick = song;
			ends = play(song);
		}

		plasma.draw();

		const bool second = ((globalTicks - start) / 8000) & 1;
		const char* const* page = second ? page2 : page1;
		const int lines = 10;
		const int h = font->getHeight() + 4;
		const int step = (canvasH - 3 * h - 8) / lines;   // the credits end above the player lines
		int y = 4;
		for (int i = 0; i < lines && showText; i++, y += step)
			if (page[i][0]) font->showString(page[i], canvasW >> 1, y, alignX::Center);

		snprintf(line, sizeof line, "%s %d  %s", playing ? "PLAYING" : "STOPPED", song + 2, songs[song].name);
		font->showString(line, canvasW >> 1, canvasH - 2 * h - 4, alignX::Center);
		if (pick != song) {
			snprintf(line, sizeof line, "SELECT %d  %s", pick + 2, songs[pick].name);
			font->showString(line, canvasW >> 1, canvasH - h - 2, alignX::Center);
		} else {
			font->showString("LEFT RIGHT SELECT  A PLAY", canvasW >> 1, canvasH - h - 2, alignX::Center);
		}

	}

	// Back to the menu's own music
	oj32x_music_track(0);
	playMusic("MENUSNG.PSM", true);

	return E_NONE;

}

#endif
