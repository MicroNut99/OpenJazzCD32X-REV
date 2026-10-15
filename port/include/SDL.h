/*
 * OpenJazz CD32X - the small part of SDL 1.2 that OpenJazz uses (built with OJ_SDL1).
 * PORT32X: no real SDL on the 32X; this header + port/sdl_oj.cpp sit on the plat layer
 * (port/plat.h): frames go to plat_present, the pad becomes key events, time is plat_ticks_ms.
 * Only 8-bit (palettised) surfaces exist - the game never uses any other kind.
 */
#ifndef OJ32X_SDL_H
#define OJ32X_SDL_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef uint8_t  Uint8;
typedef int8_t   Sint8;
typedef uint16_t Uint16;
typedef int16_t  Sint16;
typedef uint32_t Uint32;
typedef int32_t  Sint32;

#ifdef __cplusplus
extern "C" {
#endif

/* ---- init ---- */
#define SDL_INIT_TIMER    0x01
#define SDL_INIT_AUDIO    0x10
#define SDL_INIT_VIDEO    0x20
#define SDL_INIT_JOYSTICK 0x200
int  SDL_Init(Uint32 flags);
void SDL_Quit(void);
const char *SDL_GetError(void);

typedef struct { Uint8 major, minor, patch; } SDL_version;
#define SDL_VERSION(x) do { (x)->major = 1; (x)->minor = 2; (x)->patch = 0; } while (0)
const SDL_version *SDL_Linked_Version(void);

/* ---- time ---- */
Uint32 SDL_GetTicks(void);
void   SDL_Delay(Uint32 ms);

/* ---- video ---- */
typedef struct SDL_Rect { Sint16 x, y; Uint16 w, h; } SDL_Rect;
typedef struct SDL_Color { Uint8 r, g, b, unused; } SDL_Color;
typedef struct SDL_Palette { int ncolors; SDL_Color *colors; } SDL_Palette;
typedef struct {
	SDL_Palette *palette;
	Uint8  BitsPerPixel, BytesPerPixel;   /* (no colour masks - 8-bit only, 16 bytes per surface) */
	Uint32 colorkey;
} SDL_PixelFormat;
typedef struct SDL_Surface {
	Uint32 flags;
	SDL_PixelFormat *format;
	int w, h;
	Uint16 pitch;
	void *pixels;
	SDL_Rect clip_rect;
	int refcount;
} SDL_Surface;

#define SDL_SWSURFACE   0x00000000
#define SDL_HWSURFACE   0x00000001
#define SDL_DOUBLEBUF   0x40000000
#define SDL_FULLSCREEN  0x80000000
#define SDL_HWPALETTE   0x20000000
#define SDL_RESIZABLE   0x00000010
#define SDL_SRCCOLORKEY 0x00001000
#define SDL_LOGPAL 0x01
#define SDL_PHYSPAL 0x02
#define SDL_MUSTLOCK(s) 0
#define SDL_ENABLE  1
#define SDL_DISABLE 0

SDL_Surface *SDL_SetVideoMode(int w, int h, int bpp, Uint32 flags);
SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int w, int h, int bpp, Uint32 r, Uint32 g, Uint32 b, Uint32 a);
void SDL_FreeSurface(SDL_Surface *s);
static inline int  SDL_LockSurface(SDL_Surface *s) { (void)s; return 0; }
static inline void SDL_UnlockSurface(SDL_Surface *s) { (void)s; }
int  SDL_SetClipRect(SDL_Surface *s, const SDL_Rect *r);
int  SDL_SetColorKey(SDL_Surface *s, Uint32 flag, Uint32 key);
int  SDL_SetPalette(SDL_Surface *s, int flags, const SDL_Color *colors, int first, int n);
static inline int SDL_SetColors(SDL_Surface *s, const SDL_Color *c, int first, int n)
	{ return SDL_SetPalette(s, SDL_LOGPAL | SDL_PHYSPAL, c, first, n); }
Uint32 SDL_MapRGB(const SDL_PixelFormat *f, Uint8 r, Uint8 g, Uint8 b);
int  SDL_FillRect(SDL_Surface *s, SDL_Rect *r, Uint32 colour);
int  SDL_UpperBlit(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect);
#define SDL_BlitSurface SDL_UpperBlit
int  SDL_Flip(SDL_Surface *screen);
SDL_Rect **SDL_ListModes(SDL_PixelFormat *f, Uint32 flags);
static inline int  SDL_ShowCursor(int t) { (void)t; return 0; }
static inline void SDL_WM_SetCaption(const char *a, const char *b) { (void)a; (void)b; }

/* ---- keys (only the ones OpenJazz names) ---- */
typedef enum {
	SDLK_UNKNOWN = 0, SDLK_BACKSPACE = 8, SDLK_TAB = 9, SDLK_RETURN = 13, SDLK_ESCAPE = 27,
	SDLK_SPACE = 32, SDLK_0 = 48, SDLK_1, SDLK_2, SDLK_3, SDLK_4, SDLK_5, SDLK_6, SDLK_7, SDLK_8, SDLK_9,
	SDLK_a = 97, SDLK_b, SDLK_c, SDLK_d, SDLK_e, SDLK_f, SDLK_g, SDLK_h, SDLK_i, SDLK_j, SDLK_k, SDLK_l,
	SDLK_m, SDLK_n, SDLK_o, SDLK_p, SDLK_q, SDLK_r, SDLK_s, SDLK_t, SDLK_u, SDLK_v, SDLK_w, SDLK_x,
	SDLK_y, SDLK_z, SDLK_DELETE = 127,
	SDLK_UP = 273, SDLK_DOWN, SDLK_RIGHT, SDLK_LEFT, SDLK_INSERT, SDLK_HOME, SDLK_END,
	SDLK_PAGEUP, SDLK_PAGEDOWN, SDLK_F1, SDLK_F2, SDLK_F3, SDLK_F4, SDLK_F5, SDLK_F6, SDLK_F7,
	SDLK_F8, SDLK_F9, SDLK_F10, SDLK_F11, SDLK_F12,
	SDLK_RSHIFT = 303, SDLK_LSHIFT, SDLK_RCTRL, SDLK_LCTRL, SDLK_RALT, SDLK_LALT,
	SDLK_LAST = 323
} SDLKey;
/* OpenJazz writes some keys in SDL 2/3 spelling */
#define SDLK_P SDLK_p
#define SDLK_Y SDLK_y
#define SDLK_N SDLK_n
typedef enum { KMOD_NONE = 0, KMOD_LALT = 0x100, KMOD_RALT = 0x200, KMOD_ALT = 0x300 } SDLMod;
typedef struct { Uint8 scancode; SDLKey sym; SDLMod mod; Uint16 unicode; } SDL_keysym;
char *SDL_GetKeyName(SDLKey key);

/* ---- events ---- */
enum {
	SDL_NOEVENT = 0, SDL_KEYDOWN = 2, SDL_KEYUP, SDL_MOUSEMOTION, SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP,
	SDL_JOYAXISMOTION, SDL_JOYBALLMOTION, SDL_JOYHATMOTION, SDL_JOYBUTTONDOWN, SDL_JOYBUTTONUP,
	SDL_QUIT, SDL_VIDEORESIZE = 16, SDL_VIDEOEXPOSE
};
#define SDL_PRESSED  1
#define SDL_RELEASED 0
#define SDL_HAT_CENTERED 0
#define SDL_HAT_UP    1
#define SDL_HAT_RIGHT 2
#define SDL_HAT_DOWN  4
#define SDL_HAT_LEFT  8
#define SDL_BUTTON(x) (1 << ((x) - 1))
#define SDL_BUTTON_LEFT 1
typedef struct { Uint8 type, which, state; SDL_keysym keysym; } SDL_KeyboardEvent;
typedef struct { Uint8 type, which, state; Uint16 x, y; Sint16 xrel, yrel; } SDL_MouseMotionEvent;
typedef struct { Uint8 type, which, button, state; Uint16 x, y; } SDL_MouseButtonEvent;
typedef struct { Uint8 type, which, axis; Sint16 value; } SDL_JoyAxisEvent;
typedef struct { Uint8 type, which, hat, value; } SDL_JoyHatEvent;
typedef struct { Uint8 type, which, button, state; } SDL_JoyButtonEvent;
typedef struct { Uint8 type; int w, h; } SDL_ResizeEvent;
typedef union SDL_Event {
	Uint8 type;
	SDL_KeyboardEvent key;
	SDL_MouseMotionEvent motion;
	SDL_MouseButtonEvent button;
	SDL_JoyAxisEvent jaxis;
	SDL_JoyHatEvent jhat;
	SDL_JoyButtonEvent jbutton;
	SDL_ResizeEvent resize;
} SDL_Event;
int SDL_PollEvent(SDL_Event *e);
static inline int SDL_EnableUNICODE(int e) { (void)e; return 0; }
static inline int SDL_EnableKeyRepeat(int a, int b) { (void)a; (void)b; return 0; }

typedef struct _SDL_Joystick SDL_Joystick;
static inline int SDL_NumJoysticks(void) { return 0; }
static inline SDL_Joystick *SDL_JoystickOpen(int i) { (void)i; return NULL; }
static inline void SDL_JoystickClose(SDL_Joystick *j) { (void)j; }

/* ---- audio (port/sdl_oj.cpp; the 32X mixes on the slave SH2) ---- */
#define AUDIO_U8     0x0008
#define AUDIO_S8     0x8008
#define AUDIO_U16LSB 0x0010
#define AUDIO_S16LSB 0x8010
#define AUDIO_U16MSB 0x1010
#define AUDIO_S16MSB 0x9010
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define AUDIO_S16SYS AUDIO_S16MSB
#define AUDIO_U16SYS AUDIO_U16MSB
#else
#define AUDIO_S16SYS AUDIO_S16LSB
#define AUDIO_U16SYS AUDIO_U16LSB
#endif
#define SDL_MIX_MAXVOLUME 128
typedef struct {
	int freq; Uint16 format; Uint8 channels, silence; Uint16 samples, padding; Uint32 size;
	void (*callback)(void *userdata, Uint8 *stream, int len);
	void *userdata;
} SDL_AudioSpec;
int  SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained);
void SDL_PauseAudio(int pause_on);
void SDL_CloseAudio(void);
void SDL_LockAudio(void);
void SDL_UnlockAudio(void);
void SDL_MixAudio(Uint8 *dst, const Uint8 *src, Uint32 len, int volume);

#ifdef __cplusplus
}
#endif

#endif
