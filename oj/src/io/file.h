
/**
 *
 * @file file.h
 *
 * Part of the OpenJazz project
 *
 * @par History:
 * - 23rd August 2005: Created OpenJazz.h
 * - 3rd February 2009: Created file.h from parts of OpenJazz.h
 *
 * @par Licence:
 * Copyright (c) 2005-2010 AJ Thomson
 * Copyright (c) 2015-2026 Carsten Teibes
 *
 * OpenJazz is distributed under the terms of
 * the GNU General Public License, version 2.0
 *
 */


#ifndef _FILE_H
#define _FILE_H


#include "OpenJazz.h"
#include "io/gfx/video.h"

#include <stdio.h>
#include <memory>

// Types

enum pathType {
	PATH_TYPE_INVALID = 0,     ///< Invalid directory, do not use
	PATH_TYPE_SYSTEM = 1 << 0, ///< Read-only system directory
	PATH_TYPE_CONFIG = 1 << 1, ///< User writable configuration directory
	PATH_TYPE_GAME = 1 << 2,   ///< Directory containing game data
	PATH_TYPE_TEMP = 1 << 3,   ///< User writable temporary directory
	PATH_TYPE_ANY =  1 << 4    ///< Special case: any type
};

// Classes

struct SDL_Surface;
struct SDL_Color;

/// File i/o
class File {

	private:
		FILE* file;
		char* filePath;
		bool  forWriting;
#ifdef OJ32X
		const unsigned char* packData; ///< PORT32X: start of the file in a cart pack, or null
#endif

		bool open (const char* path, const char* name, bool write);

	public:
		File                           (const char* name, int pathType, bool write = false);
		~File                          ();

		static bool        exists      (const char *fileName, int pathType = PATH_TYPE_ANY);

		int                getSize     ();
		void               seek        (int offset, bool reset);
		int                seek        (int offset, int origin = SEEK_CUR);
		int                tell        ();
		int                read        (void *buffer, int size, int count);
		unsigned char      loadChar    ();
		void               storeChar   (const unsigned char val);
		unsigned short int loadShort   ();
		unsigned short int loadShort   (unsigned short int max);
		void               storeShort  (const unsigned short int val);
		signed int         loadInt     ();
		void               storeInt    (const signed int val);
		char*              loadString  (int length);
		void               storeString (const char *string);
		void               storeData   (const void* data, int length);
		unsigned char*     loadBlock   (int length);
#ifdef OJ32X
		/// PORT32X: like loadBlock, but a file from a cart pack gives a pointer into the cart
		/// (read-only, never freed: check with oj32xIsPackData before delete[])
		unsigned char*     mapBlock    (int length);
		/// PORT32X: the picture at the current position, already decoded in a cart pack
		/// (the file then continues after its <length> bytes); null if not there
		const unsigned char* packPicture (int width, int height, int length);
		/// PORT32X: the picture at the current position in a cart pack, any size
		const unsigned char* packPictureAny (int* width, int* height, uint32_t* end);
		/// PORT32X: an RLE block of <length> bytes - in place from a cart pack (mapped = true,
		/// never delete[] it) or decoded as loadRLE does (mapped = false); the PC build records it
		unsigned char*     loadRLEPacked (int length, bool& mapped);
		/// PORT32X: PC build - record the picture decoded from file bytes <start>..now for the
		/// pack being baked (T32X_BAKE); does nothing on the 32X
		void               bakePicture (int start, const unsigned char* pixels, int width, int height);
#endif
		unsigned char*     loadRLE     (int length, bool checkSize = true);
		void               skipRLE     ();
		unsigned char*     loadLZ      (int compressedLength, int length);
		char*              loadTerminatedString (int maxSize = 0);
		char*              loadFileName();
		SDL_Surface*       loadSurface (int width, int height, bool checkSize = true);
		unsigned char*     loadPixels  (int length);
		unsigned char*     loadPixels  (int length, int key);
		void               loadPalette (SDL_Color* palette, bool rle = true);

};

using FilePtr = std::unique_ptr<File>;


/// Directory path

class Path {

	public:
		Path* next;      ///< Next path to check
		char* path;      ///< Path
		int   pathType;  ///< One or more of path_type enum

		Path  (Path* newNext, char* newPath, int newPathType);
		~Path ();

};


class PathMgr {

	public:
		PathMgr();
		~PathMgr();

		bool add(char* newPath, int newPathType = PATH_TYPE_ANY);
		const char* getTemp();

		Path* paths;

		bool has_config;
		bool has_temp;

};


// Directory Seperator

#ifdef _WIN32
	#define OJ_DIR_SEP '\\'
	#define OJ_DIR_SEP_STR "\\"
#else
	#define OJ_DIR_SEP '/'
	#define OJ_DIR_SEP_STR "/"
#endif


// Variable

EXTERN PathMgr gamePaths; ///< Paths to files

#endif

