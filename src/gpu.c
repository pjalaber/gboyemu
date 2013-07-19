#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <SDL.h>
#include "gpu.h"
#include "mmu.h"
#include "interrupt.h"

#define GB_SCREEN_TILES_COUNT_W (GB_SCREEN_WIDTH / 8)
#define GB_SCREEN_TILES_COUNT_H (GB_SCREEN_HEIGHT / 8)

#define GPU_LY_VBLANK_START GB_SCREEN_HEIGHT
#define GPU_LY_VBLANK_END (GPU_LY_VBLANK_START + GPU_RPT_MODE_1)

#define SDL_BYTES_PER_PIXEL 4
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
#define SDL_COLOR_BUILD(r, g, b, a) (((r) << 24) | ((g) << 16) | ((b) << 8) || (a))
#define SDL_COLOR_GET_ALPHA(c) ((c) & 0xFF)
#define SDL_COLOR_SET_ALPHA(c, a) (c) = ((c) & 0xFFFFFF00) | (a)
#else
#define SDL_COLOR_BUILD(r, g, b, a) (((a) << 24) | ((b) << 16) | ((g) << 8) | (r))
#define SDL_COLOR_GET_ALPHA(c) (((c) >> 24) & 0xFF)
#define SDL_COLOR_SET_ALPHA(c, a) (c) = ((c) & 0x00FFFFFF) | ((a) << 24)
#endif

static SDL_Surface *screen = NULL;
static SDL_Surface *gb_surface = NULL;
static SDL_Rect screen_rect;

struct sprite
{
	uint8_t y;
	uint8_t x;
	uint8_t tile;
#define SPRITE_BEHIND_BG 0x80
#define SPRITE_YFLIP	 0x40
#define SPRITE_XFLIP	 0x20
#define SPRITE_PALETTE   0x10
	uint8_t flags;
};

static struct
{
	int32_t cycles;
	uint8_t ly, lycmp;
	uint8_t bgp;

#define LCDCTRL_LCD_ON			       0x80
#define LCDCTRL_WINDOW_TILE_MAP		       0x40
#define LCDCTRL_WINDOW_ON		       0x20
#define LCDCTRL_BG_AND_WINDOW_TILE_SET	       0x10
#define LCDCTRL_BG_TILE_MAP		       0x08
#define LCDCTRL_SPRITE_8x16		       0x04
#define LCDCTRL_SPRITE_ON		       0x02
#define LCDCTRL_BG_ON			       0x01
	uint8_t lcdctrl;

#define LCDSTATUS_LY_COINCIDENCE_INTERRUPT     0x40
#define LCDSTATUS_MODE2_OAM_INTERRUPT	       0x20
#define LCDSTATUS_MODE1_VBLANK_INTERRUPT       0x10
#define LCDSTATUS_MODE0_HBLANK_INTERRUPT       0x08
#define LCDSTATUS_COINCIDENCE_FLAG	       0x04
#define LCDSTATUS_MODE_FLAG		       0x03
	uint8_t lcdstatus;
	uint8_t objpal[2];
	uint8_t scrollx, scrolly;
	uint8_t windowx, windowy;
	uint8_t vram[0x2000];
#define MAX_SPRITES 40
	uint8_t oam[MAX_SPRITES * sizeof(struct sprite)];
} gpu;

static struct
{
	uint32_t bgp_sdl[4];
	uint32_t objpal_sdl[2][4];

#define MAX_TILES 384
	/* pre-computed tiles with zoom applied (only on x-axis)
	 */
	uint8_t tiles[MAX_TILES][64*GPU_ZOOM_MAX];
} gpu_cache;

static struct
{
	uint32_t current;
	uint32_t requested;
} gpu_zoom;

/* how-to blend two pixels together
 */
#define BLENDING_SRC_OPAQUE	 0x1
#define BLENDING_DST_TRANSPARENT 0x2

#define max(a, b) ((a) < (b) ? (b) : (a))
#define min(a, b) ((a) < (b) ? (a) : (b))

static const uint32_t gpu_mode_cycles[] =
{
	GPU_CYCLES_MODE_0, GPU_CYCLES_MODE_1, GPU_CYCLES_MODE_2, GPU_CYCLES_MODE_3
};

static const uint8_t gpu_grey_colors[] =
{
	255, 192, 96, 0
};

static void gpu_set_ly(uint8_t ly);

static uint32_t gpu_adjust_zoom(uint32_t zoom)
{
	if ( zoom == 0 || zoom > GPU_ZOOM_MAX )
		zoom = GPU_ZOOM_DEFAULT;
	return zoom;
}

static int32_t gpu_create_gb_surface(void)
{
	uint32_t rmask, gmask, bmask, amask;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	rmask = 0xff000000;
	gmask = 0x00ff0000;
	bmask = 0x0000ff00;
	amask = 0x000000ff;
#else
	rmask = 0x000000ff;
	gmask = 0x0000ff00;
	bmask = 0x00ff0000;
	amask = 0xff000000;
#endif

	if ( gb_surface != NULL )
		SDL_FreeSurface(gb_surface);

	gb_surface = SDL_CreateRGBSurface(SDL_SWSURFACE, GB_SCREEN_WIDTH * gpu_zoom.current, GB_SCREEN_HEIGHT * gpu_zoom.current,
		SDL_BYTES_PER_PIXEL * 8, rmask, gmask, bmask, amask);

	if ( gb_surface == NULL )
	{
		fprintf(stderr, "Can't create gameboy SDL surface: %s\n", SDL_GetError());
		return -1;
	}

	SDL_SetAlpha(gb_surface, 0, SDL_ALPHA_OPAQUE);

	if ( gb_surface->format->BytesPerPixel != SDL_BYTES_PER_PIXEL )
	{
		fprintf(stderr, "Unattended bytes per pixel value: %u\n", gb_surface->format->BytesPerPixel);
		return -1;
	}

	screen_rect.x = (GPU_ZOOM_MAX - gpu_zoom.current) * GB_SCREEN_WIDTH / 2;
	screen_rect.y = (GPU_ZOOM_MAX - gpu_zoom.current) * GB_SCREEN_HEIGHT / 2;
	screen_rect.w = GB_SCREEN_WIDTH * gpu_zoom.current;
	screen_rect.h = GB_SCREEN_HEIGHT * gpu_zoom.current;

	SDL_FillRect(screen, NULL, SDL_COLOR_BUILD(0xE0, 0xE0, 0xE0, SDL_ALPHA_OPAQUE));
	SDL_Flip(screen);

	return 0;
}

int32_t gpu_init(uint32_t zoom)
{
	memset(&gpu, 0, sizeof(gpu));
	memset(&gpu_cache, 0, sizeof(gpu_cache));
	gpu.lcdctrl = LCDCTRL_LCD_ON | LCDCTRL_BG_AND_WINDOW_TILE_SET | LCDCTRL_BG_ON;
	gpu.lcdstatus = 0x02;
	gpu_set_ly(0);
	gpu_zoom.current = gpu_adjust_zoom(zoom);
	gpu_zoom.requested = gpu_zoom.current;

	screen = SDL_SetVideoMode(GB_SCREEN_WIDTH * GPU_ZOOM_MAX, GB_SCREEN_HEIGHT * GPU_ZOOM_MAX,
			SDL_BYTES_PER_PIXEL * 8, SDL_HWSURFACE | SDL_DOUBLEBUF);

	if ( screen == NULL )
	{
		fprintf(stderr, "Can't set SDL video mode: %s\n", SDL_GetError());
		return -1;
	}

	if ( screen->format->BytesPerPixel != SDL_BYTES_PER_PIXEL )
	{
		fprintf(stderr, "Unattended bytes per pixel value: %u\n", screen->format->BytesPerPixel);
		return -1;
	}

	if ( gpu_create_gb_surface() < 0 )
		return -1;

	return 0;
}

static void gpu_blank_curline(void)
{
	SDL_Rect rect;
	assert(gpu.ly < GB_SCREEN_HEIGHT);
	rect.x = 0;
	rect.y = gpu.ly * gpu_zoom.current;
	rect.w = GB_SCREEN_WIDTH * gpu_zoom.current;
	rect.h = gpu_zoom.current;
	if ( SDL_FillRect(gb_surface, &rect, SDL_COLOR_BUILD(0xFF, 0xFF, 0xFF, SDL_ALPHA_OPAQUE)) < 0 )
		fprintf(stderr, "Can't blank current line: %s\n", SDL_GetError());
}

static void gpu_blank_gb_surface(void)
{
	/* fill the entire surface
	 */
	if ( SDL_FillRect(gb_surface, NULL, SDL_COLOR_BUILD(0xFF, 0xFF, 0xFF, SDL_ALPHA_OPAQUE)) < 0 )
		fprintf(stderr, "Can't blank gb_surface: %s\n", SDL_GetError());
}

static inline uint32_t gpu_blending(uint32_t dst, uint32_t src, uint8_t blending)
{
	if ( (blending & BLENDING_SRC_OPAQUE) == BLENDING_SRC_OPAQUE )
		if ( SDL_COLOR_GET_ALPHA(src) != SDL_ALPHA_OPAQUE )
			return dst;

	if ( (blending & BLENDING_DST_TRANSPARENT) == BLENDING_DST_TRANSPARENT )
		if ( SDL_COLOR_GET_ALPHA(dst) != SDL_ALPHA_TRANSPARENT )
			return dst;

	return src;
}

/* blit src to dst
 */
static void *gpu_blit(void *dst, const void *src, size_t n, uint32_t reverse_src, uint8_t blending,
	const uint32_t pal[static 4])
{
	uint32_t *d;
	const char *s;


	d = dst;
	if ( reverse_src )
		s = src - 1;
	else
		s = src;

	while ( n > 0 )
	{
		assert(*s <= 3);
		*d = gpu_blending(*d, pal[(uint8_t)*s], blending);
		d++;
		if ( reverse_src )
			s--;
		else
			s++;
		n--;
	}

	return dst;
}

static inline void gpu_update_coincidence_flag(void)
{
	if ( gpu.lycmp == gpu.ly )
	{
		if ( (gpu.lcdstatus & LCDSTATUS_COINCIDENCE_FLAG) == 0 )
		{
			gpu.lcdstatus |= LCDSTATUS_COINCIDENCE_FLAG;
			if ( (gpu.lcdstatus & LCDSTATUS_LY_COINCIDENCE_INTERRUPT) == LCDSTATUS_LY_COINCIDENCE_INTERRUPT )
				interrupt_request(INTERRUPT_LCDSTAT);
		}
	}
	else
	{
		gpu.lcdstatus &= ~LCDSTATUS_COINCIDENCE_FLAG;
	}
}

static void gpu_set_ly(uint8_t ly)
{
	gpu.ly = ly;
	gpu_update_coincidence_flag();
}

static void gpu_set_lycmp(uint8_t lycmp)
{
	gpu.lycmp = lycmp;
	gpu_update_coincidence_flag();
}


uint8_t gpu_read_ly(void)
{
	return gpu.ly;
}

void gpu_write_ly(uint8_t value)
{
	/* writing reset the counter
	 */
	gpu_set_ly(0);
}

uint8_t gpu_read_lycmp()
{
	return gpu.lycmp;
}

void gpu_write_lycmp(uint8_t lycmp)
{
	gpu_set_lycmp(lycmp);
}

static uint32_t gpu_compute_sdl_color(uint8_t src, uint8_t bit_index)
{
	uint32_t sdl_color;
	uint8_t rgb, alpha;
	rgb = gpu_grey_colors[(src >> (bit_index * 2)) & 0x3];
	alpha = (bit_index == 0) ? SDL_ALPHA_TRANSPARENT : SDL_ALPHA_OPAQUE;
	sdl_color = SDL_COLOR_BUILD(rgb, rgb, rgb, alpha);
	return sdl_color;
}

void gpu_write_bgp(uint8_t bgp)
{
	/* FF47 - BGP - BG Palette Data (R/W) - Non CGB Mode Only
	 * This register assigns gray shades to the color numbers of the BG and Window tiles.
	 * Bit 7-6 - Shade for Color Number 3
	 * Bit 5-4 - Shade for Color Number 2
	 * Bit 3-2 - Shade for Color Number 1
	 * Bit 1-0 - Shade for Color Number 0
	 *
	 * The four possible gray shades are:
	 * 0  White
	 * 1  Light gray
	 * 2  Dark gray
	 * 3  Black
	 */

	int32_t i;
	gpu.bgp = bgp;
	for ( i = 0; i < 4; i++)
		gpu_cache.bgp_sdl[i] = gpu_compute_sdl_color(gpu.bgp, i);
}

uint8_t gpu_read_bgp(void)
{
	return gpu.bgp;
}

uint8_t gpu_read_lcdctrl(void)
{
	return gpu.lcdctrl;
}

void gpu_write_lcdctrl(uint8_t lcdctrl)
{
	/* is LCD turned on/off ?
	 */
	if ( (gpu.lcdctrl & LCDCTRL_LCD_ON) != (lcdctrl & LCDCTRL_LCD_ON) )
	{
		gpu_set_ly(0);
		gpu_blank_gb_surface();
	}

	gpu.lcdctrl = lcdctrl;
}

uint8_t gpu_read_lcdstatus(void)
{
	return gpu.lcdstatus;
}

void gpu_write_lcdstatus(uint8_t lcdstatus)
{
	/* 3 lower bits of gpu.lcdstatus are read-only
	 */
	gpu.lcdstatus = (gpu.lcdstatus & 0x7) | (lcdstatus & 0xF8);
}

uint8_t gpu_read_scrollx(void)
{
	return gpu.scrollx;
}

void gpu_write_scrolly(uint8_t scrolly)
{
	gpu.scrolly = scrolly;
}

void gpu_write_scrollx(uint8_t scrollx)
{
	gpu.scrollx = scrollx;
}

uint8_t gpu_read_scrolly(void)
{
	return gpu.scrolly;
}

void gpu_write_windowx(uint8_t windowx)
{
	gpu.windowx = windowx;
}

uint8_t gpu_read_windowx(void)
{
	return gpu.windowx;
}

void gpu_write_windowy(uint8_t windowy)
{
	gpu.windowy = windowy;
}

uint8_t gpu_read_windowy(void)
{
	return gpu.windowy;
}

void gpu_write_objpal0(uint8_t objpal0)
{
	int32_t i;
	gpu.objpal[0] = objpal0;
	for ( i = 0; i < 4; i++)
		gpu_cache.objpal_sdl[0][i] = gpu_compute_sdl_color(gpu.objpal[0], i);
}

void gpu_write_objpal1(uint8_t objpal1)
{
	int32_t i;
	gpu.objpal[1] = objpal1;
	for ( i = 0; i < 4; i++)
		gpu_cache.objpal_sdl[1][i] = gpu_compute_sdl_color(gpu.objpal[1], i);

}

uint8_t gpu_read_objpal0(void)
{
	return gpu.objpal[0];
}

uint8_t gpu_read_objpal1(void)
{
	return gpu.objpal[1];
}

static inline void gpu_compute_tile(uint32_t tile)
{
	uint16_t address;
	uint8_t *ptr, byte0, byte1;
	uint32_t byte;

	/* VRAM Tile Data
	 * Two Tile Pattern Tables at $8000-8FFF and at $8800-97FF.
	 * Each tile is sized 8x8 pixels and has a color depth of 4 colors/gray shades.
	 * The first one can be used for sprites and the background. Its tiles are numbered from 0 to 255.
	 * The second table can be used for the background and the window display and its tiles are
	 * numbered from -128 to 127.
	 * 8*8 pixels * 4 colors = 64 * 2 bits = 16 bytes per tile
	 */

	assert(tile < MAX_TILES);
	ptr = gpu_cache.tiles[tile];
	address = tile * 16;

	for ( byte = 0; byte < 8; byte++ )
	{
		byte0 = gpu.vram[address++];
		byte1 = gpu.vram[address++];

		memset(&ptr[0*gpu_zoom.current], ((byte0 & 0x80) >> 7) | ((byte1 & 0x80) >> 6), gpu_zoom.current);

		memset(&ptr[1*gpu_zoom.current], ((byte0 & 0x40) >> 6) | ((byte1 & 0x40) >> 5), gpu_zoom.current);

		memset(&ptr[2*gpu_zoom.current], ((byte0 & 0x20) >> 5) | ((byte1 & 0x20) >> 4), gpu_zoom.current);

		memset(&ptr[3*gpu_zoom.current], ((byte0 & 0x10) >> 4) | ((byte1 & 0x10) >> 3), gpu_zoom.current);

		memset(&ptr[4*gpu_zoom.current], ((byte0 & 0x08) >> 3) | ((byte1 & 0x08) >> 2), gpu_zoom.current);

		memset(&ptr[5*gpu_zoom.current], ((byte0 & 0x04) >> 2) | ((byte1 & 0x04) >> 1), gpu_zoom.current);

		memset(&ptr[6*gpu_zoom.current], ((byte0 & 0x02) >> 1) | ((byte1 & 0x02) >> 0), gpu_zoom.current);

		memset(&ptr[7*gpu_zoom.current], ((byte0 & 0x01) >> 0) | ((byte1 & 0x01) << 1), gpu_zoom.current);

		ptr += 8 * gpu_zoom.current;
	}
}

static void gpu_display_tile_line(uint32_t x, uint32_t y, uint32_t tile_number,
	uint8_t tile_x_offset, uint8_t tile_y_offset,
	uint8_t xflip, uint8_t yflip, uint8_t blending,
	const uint32_t pal[static 4])
{
	uint32_t a, i;
	uint8_t *pixels = (uint8_t *)gb_surface->pixels;

	assert(tile_number < MAX_TILES);
	assert(tile_x_offset < 8);
	assert(tile_y_offset < 8);

	assert(x < GB_SCREEN_WIDTH && y < GB_SCREEN_HEIGHT);

	if ( x + 8 - tile_x_offset <= GB_SCREEN_WIDTH )
		a = 8 - tile_x_offset;
	else
		a = GB_SCREEN_WIDTH - x + tile_x_offset;

	if ( yflip )
		tile_y_offset = 7 - tile_y_offset;

	if ( xflip )
		tile_x_offset = 8 - tile_x_offset;

	for ( i = 0; i < gpu_zoom.current; i++ )
	{
		gpu_blit(&pixels[(y * gpu_zoom.current * gb_surface->pitch) + (i * gb_surface->pitch) + (x * gpu_zoom.current * SDL_BYTES_PER_PIXEL)],
			&gpu_cache.tiles[tile_number][(tile_y_offset * 8 + tile_x_offset) * gpu_zoom.current],
			a * gpu_zoom.current, xflip, blending, pal);
	}
}

static uint32_t gpu_y_totile(uint8_t y, uint8_t *offsety)
{
	uint32_t yy;
	yy = gpu.scrolly + y;
	if ( yy >= 256 )
		yy -= 256;
	*offsety = yy % 8;
	return yy / 8;
}

static uint32_t gpu_x_totile(uint8_t x, uint8_t *offsetx)
{
	uint32_t xx;
	xx = gpu.scrollx + x;
	if ( xx >= 256 )
		xx -= 256;
	*offsetx = xx % 8;
	return xx / 8;
}

static void gpu_display_background(void)
{
	uint32_t vtile, htile, x;
	uint8_t offsety, offsetx;
	uint32_t tile_number;
	uint16_t address;

	assert(gpu.ly < GB_SCREEN_HEIGHT);

	if ( (gpu.lcdctrl & LCDCTRL_BG_ON) == 0 )
	{
		gpu_blank_curline();
		return;
	}

	/* VRAM Background Maps
	 * The gameboy contains two 32x32 tile background maps in VRAM at addresses 9800h-9BFFh and 9C00h-9FFFh.
	 * Each can be used either to display "normal" background, or "window" background.
	 * Each byte contains a number of a tile to be displayed. Tile patterns are taken from the Tile Data Table
	 * located either at $8000-8FFF or $8800-97FF. In the first case, patterns are numbered with unsigned numbers
	 * from 0 to 255 (i.e. pattern #0 lies at address $8000). In the second case, patterns have signed numbers
	 * from -128 to 127 (i.e. pattern #0 lies at address $9000).
	 * The Tile Data Table address for the background can be selected via LCDC register
	 */

	if ( (gpu.lcdctrl & LCDCTRL_BG_TILE_MAP) == LCDCTRL_BG_TILE_MAP )
		address = 0x1C00;
	else
		address = 0x1800;

	vtile = gpu_y_totile(gpu.ly, &offsety);

	for ( x = 0; x < GB_SCREEN_WIDTH; x += 8 - offsetx )
	{
		htile = gpu_x_totile(x, &offsetx);
	 	tile_number = gpu.vram[address + ((vtile * 32) + htile)];
		if ( (gpu.lcdctrl & LCDCTRL_BG_AND_WINDOW_TILE_SET) == 0 )
			tile_number = 256 + (int8_t)tile_number;

		gpu_display_tile_line(
			x, 		/* x gb surface */
			gpu.ly,     	/* y gb surface */
			tile_number,
			offsetx,	/* x tile offset */
			offsety,	/* y tile offset */
			0,		/* x-flip disabled */
			0,		/* y-flip disabled */
			0,		/* blending */
			gpu_cache.bgp_sdl	/* background palette */
			);
	}
}

static void gpu_display_window(void)
{
	uint16_t address;
	uint8_t offsety;
	int32_t x, y, vtile, htile;
	uint32_t tile_number;

	/* The Window
	 * Besides background, there is also a "window" overlaying the background. The window is not scrollable i.e.
	 * it is always displayed starting from its left upper corner. The location of a window on the screen can be
	 * adjusted via WX and WY registers. Screen coordinates of the top left corner of a window are WX-7,WY.
	 * The tiles for the window are stored in the Tile Data Table.
	 * Both the Background and the window share the same Tile Data Table.
	 */

	assert(gpu.ly < GB_SCREEN_HEIGHT);

	if ( (gpu.lcdctrl & LCDCTRL_WINDOW_ON) == 0 )
		return;

	if ( (gpu.lcdctrl & LCDCTRL_WINDOW_TILE_MAP) == LCDCTRL_WINDOW_TILE_MAP )
		address = 0x1C00;
	else
		address = 0x1800;

	x = gpu.windowx - 7;
	y = gpu.windowy;

	/* is window visible ?
	 */
	if ( x >= GB_SCREEN_WIDTH || gpu.windowy >= GB_SCREEN_HEIGHT )
		return;

	/* does current line fit into the window ?
	 */
	if ( gpu.ly < y )
		return;

	vtile = (gpu.ly - y) / 8;
	offsety = (gpu.ly - y) % 8;
	htile = 0;
	for ( ; x < GB_SCREEN_WIDTH; x += 8 )
	{
	 	tile_number = gpu.vram[address + ((vtile * 32) + htile)];
		if ( (gpu.lcdctrl & LCDCTRL_BG_AND_WINDOW_TILE_SET) == 0 )
			tile_number = 256 + (int8_t)tile_number;

		gpu_display_tile_line(
			max(x, 0),	/* x gb surface */
			gpu.ly,     	/* y gb surface */
			tile_number,
			-min(x, 0),	/* x tile offset */
			offsety,	/* y tile offset */
			0,		/* x-flip disabled */
			0,		/* y-flip disabled */
			0,		/* blending */
			gpu_cache.bgp_sdl	/* background palette */
			);
		htile++;
	}
}

/* compare two sprites according to their priority
 * return +1 if priority(sprite1) < priority(sprite2)
 * return -1 if priority(sprite1) > priority(sprite2)
 */
static int32_t gpu_sprite_priority(const void *s1, const void *s2)
{
	struct sprite * const *sprite1 = s1;
	struct sprite * const *sprite2 = s2;

	if ( (*sprite1)->x < (*sprite2)->x )
		return 1;
	else if ( (*sprite1)->x > (*sprite2)->x )
		return -1;
	else if ( *sprite1 < *sprite2 )
		return 1;
	else
		return -1;
}

static void gpu_display_sprites(void)
{
	int32_t i, x, y;
	int32_t sprites_count, sprite_start, sprite_end, tile_number;
	struct sprite *sprite;
	uint8_t blending;
	uint8_t palette, offsety;
	uint8_t yflip;
	struct sprite *sprites[MAX_SPRITES];

#define TILE0(tile) ((tile) & 0xFE)
#define TILE1(tile) ((tile) | 0x01)

	if ( (gpu.lcdctrl & LCDCTRL_SPRITE_ON) == 0 )
		return;

	/* select all sprites that fit on current display line gpu.ly
	 */
	sprites_count = 0;
	for ( i = 0; i < MAX_SPRITES; i++ )
	{
		sprite = (struct sprite *)&gpu.oam[i * sizeof(struct sprite)];

		x = sprite->x - 8;
		y = sprite->y - 16;

		/* current sprite is not on line gpu.ly ?
		 */
		if ( (gpu.lcdctrl & LCDCTRL_SPRITE_8x16) == LCDCTRL_SPRITE_8x16 )
		{
			if ( gpu.ly < y || gpu.ly > y + 15 )
				continue;
		}
		else
		{
			if ( gpu.ly < y || gpu.ly > y + 7 )
				continue;
		}

		/* current sprite is not visible ?
		 */
		if ( sprite->y == 0 || sprite->y >= GB_SCREEN_HEIGHT + 16 )
			continue;

		sprites[sprites_count] = sprite;
		sprites_count++;
	}

	/* sort sprites by lower to higher priority
	 */
	qsort(sprites, sprites_count, sizeof(struct sprite *), gpu_sprite_priority);

	/* now display max 10 sprites with highest priorities
	 * on current line
	 */
	sprite_start = max(sprites_count - 10, 0);
	sprite_end = sprites_count;

	for ( i = sprite_start; i < sprite_end; i++ )
	{
		sprite = sprites[i];

		/* current sprite is not visible ?
		 */
		if ( sprite->x == 0 || sprite->x >= GB_SCREEN_WIDTH + 8 )
			continue;

		x = sprite->x - 8;
		y = sprite->y - 16;

		if ( (sprite->flags & SPRITE_BEHIND_BG) == SPRITE_BEHIND_BG )
			blending = BLENDING_SRC_OPAQUE | BLENDING_DST_TRANSPARENT;
		else
			blending = BLENDING_SRC_OPAQUE;

		if ( (sprite->flags & SPRITE_PALETTE) == SPRITE_PALETTE )
			palette = 1;
		else
			palette = 0;

		yflip = sprite->flags & SPRITE_YFLIP;

		if ( (gpu.lcdctrl & LCDCTRL_SPRITE_8x16) == LCDCTRL_SPRITE_8x16 )
		{
			if ( gpu.ly <= y + 7 )
			{
				if ( yflip == 0 )
					tile_number = TILE0(sprite->tile);
				else
					tile_number = TILE1(sprite->tile);
				offsety = gpu.ly - y;
			}
			else
			{
				if ( yflip == 0 )
					tile_number = TILE1(sprite->tile);
				else
					tile_number = TILE0(sprite->tile);
				offsety = gpu.ly - y - 8;
			}
		}
		else
		{
			tile_number = sprite->tile;
			offsety = gpu.ly - y;
		}

		gpu_display_tile_line(
			max(x, 0),   		/* x gb surface offset */
			gpu.ly,			/* y gb surface offset */
			tile_number,
			-min(x, 0),		/* x tile offset */
			offsety,		/* y tile offset */
			sprite->flags & SPRITE_XFLIP,
			yflip,
			blending,
			gpu_cache.objpal_sdl[palette]
			);
	}
}

void gpu_display(void)
{
	if ( (gpu.lcdctrl & LCDCTRL_LCD_ON) == 0 )
	{
		gpu_blank_curline();
		return;
	}

	SDL_LockSurface(gb_surface);
	gpu_display_background();
	gpu_display_window();
	gpu_display_sprites();
	SDL_UnlockSurface(gb_surface);
}

void gpu_set_zoom(uint32_t zoom)
{
	gpu_zoom.requested = gpu_adjust_zoom(zoom);
}

uint32_t gpu_get_zoom(void)
{
	return gpu_zoom.current;
}

void gpu_run(uint32_t cycles, uint32_t frame_skip)
{
	uint32_t mode;
	uint32_t i;

	mode = gpu.lcdstatus & LCDSTATUS_MODE_FLAG;
	gpu.cycles += cycles;

	while ( gpu.cycles >= gpu_mode_cycles[mode] )
	{
		gpu.cycles -= gpu_mode_cycles[mode];

		if ( (gpu.lcdctrl & LCDCTRL_LCD_ON) == 0 )
		{
			switch ( mode )
			{
				case 1:
				mode = 2;
				if ( (gpu.lcdstatus & LCDSTATUS_MODE2_OAM_INTERRUPT) == LCDSTATUS_MODE2_OAM_INTERRUPT )
					interrupt_request(INTERRUPT_LCDSTAT);
				break;

				case 2:
				mode = 3;
				break;

				case 3:
				mode = 0;
				if ( (gpu.lcdstatus & LCDSTATUS_MODE0_HBLANK_INTERRUPT) == LCDSTATUS_MODE0_HBLANK_INTERRUPT )
					interrupt_request(INTERRUPT_LCDSTAT);
				break;

				case 0:
				mode = 2;
				if ( (gpu.lcdstatus & LCDSTATUS_MODE2_OAM_INTERRUPT) == LCDSTATUS_MODE2_OAM_INTERRUPT )
					interrupt_request(INTERRUPT_LCDSTAT);
				break;

				default:
				assert(0);
			}
		}
		else
		{
			switch ( mode )
			{
				/* Mode 0: The LCD controller is in the H-Blank period and
				 * the CPU can access both the display RAM (0x8000h-0x9FFF)
				 * and OAM (0xFE00h-0xFE9F)
				 */
				case 0:
				gpu_set_ly(gpu.ly + 1);
				if ( gpu.ly == GPU_LY_VBLANK_START )
				{
					interrupt_request(INTERRUPT_VBLANK);
					if ( (gpu.lcdstatus & LCDSTATUS_MODE1_VBLANK_INTERRUPT) == LCDSTATUS_MODE1_VBLANK_INTERRUPT )
						interrupt_request(INTERRUPT_LCDSTAT);
					mode = 1;
				}
				else
				{
					if ( (gpu.lcdstatus & LCDSTATUS_MODE2_OAM_INTERRUPT) == LCDSTATUS_MODE2_OAM_INTERRUPT )
						interrupt_request(INTERRUPT_LCDSTAT);
					mode = 2;
				}
				break;

				/* Mode 1: The LCD controller is in the V-Blank period (or the
				 * display is disabled) and the CPU can access both the
				 * display RAM (0x8000h-0x9FFF) and OAM (0xFE00-0xFE9F)
				 */
				case 1:
				gpu_set_ly(gpu.ly + 1);
				if ( gpu.ly == GPU_LY_VBLANK_END )
				{
					gpu_set_ly(0);
					if ( (gpu.lcdstatus & LCDSTATUS_MODE2_OAM_INTERRUPT) == LCDSTATUS_MODE2_OAM_INTERRUPT )
						interrupt_request(INTERRUPT_LCDSTAT);
					SDL_BlitSurface(gb_surface, NULL, screen, &screen_rect);
					SDL_Flip(screen);
					if ( gpu_zoom.requested != gpu_zoom.current )
					{
						gpu_zoom.current = gpu_zoom.requested;
						gpu_create_gb_surface();
						for ( i = 0; i < MAX_TILES; i++ )
							gpu_compute_tile(i);
					}
					mode = 2;
				}
				break;

				/* Mode 2: The LCD controller is reading from OAM memory.
				 * The CPU <cannot> access OAM memory (0xFE00-0xFE9F)
				 * during this period.
				 */
				case 2:
				mode = 3;
				break;

				/* Mode 3: The LCD controller is reading from both OAM and VRAM,
				 * The CPU <cannot> access OAM and VRAM during this period.
				 */
				case 3:
				gpu_display();
				if ( (gpu.lcdstatus & LCDSTATUS_MODE0_HBLANK_INTERRUPT) == LCDSTATUS_MODE0_HBLANK_INTERRUPT )
					interrupt_request(INTERRUPT_LCDSTAT);
				mode = 0;
				break;

				default:
				assert(0);
			}
		}

		gpu.lcdstatus = (gpu.lcdstatus & ~LCDSTATUS_MODE_FLAG) | mode;
	}
}

void gpu_write_vram(uint16_t addr, uint8_t value)
{
	assert(addr < 0x2000);

	/* with gpu reading from VRAM memory, the cpu cannot access VRAM memory
	 */
	if ( (gpu.lcdstatus & ~LCDSTATUS_MODE_FLAG) == 3 )
		fprintf(stderr, "invalid vram write\n");

	gpu.vram[addr] = value;

	/* recompute only modified tile
	 * above 0x1FFF there is the tile background map
	 */
	if ( addr < 0x1800 )
	{
		gpu_compute_tile(addr / 16);
	}
}

uint8_t gpu_read_vram(uint16_t addr)
{
	assert(addr < 0x2000);
	/* with gpu reading from VRAM memory, the cpu cannot access VRAM memory
	 */
	return gpu.vram[addr];
}

void gpu_write_oam(uint16_t addr, uint8_t value)
{
	assert(addr < sizeof(gpu.oam));

	/* with gpu reading from OAM memory, the cpu cannot access OAM memory
	 */
	if ( (gpu.lcdstatus & ~LCDSTATUS_MODE_FLAG) == 2 || (gpu.lcdstatus & ~LCDSTATUS_MODE_FLAG) == 3 )
		fprintf(stderr, "invalid oam write\n");

	gpu.oam[addr] = value;
}

uint8_t gpu_read_oam(uint16_t addr)
{
	assert(addr < sizeof(gpu.oam));

	/* with gpu reading from OAM memory, the CPU cannot access OAM memory
	 */
	return gpu.oam[addr];
}

void gpu_start_dma(uint8_t value)
{
	uint16_t address = value * 0x100;
	uint16_t i;

	for ( i = 0; i < sizeof(gpu.oam); i++ )
	{
		gpu.oam[i] = mmu_read_mem8(address + i);
	}
}

int32_t gpu_dump(FILE *file)
{
	if ( fwrite(&gpu, 1, sizeof(gpu), file) != sizeof(gpu) )
		return -1;
	return 0;
}

int32_t gpu_restore(FILE *file)
{
	uint32_t i;
	if ( fread(&gpu, 1, sizeof(gpu), file) != sizeof(gpu) )
		return -1;

	/* update gpu cache
	 */
	for ( i = 0; i < MAX_TILES; i++ )
		gpu_compute_tile(i);

	for ( i = 0; i < 4; i++)
	{
		gpu_cache.bgp_sdl[i] = gpu_compute_sdl_color(gpu.bgp, i);
		gpu_cache.objpal_sdl[0][i] = gpu_compute_sdl_color(gpu.objpal[0], i);
		gpu_cache.objpal_sdl[1][i] = gpu_compute_sdl_color(gpu.objpal[1], i);
	}

	if ( (gpu.lcdctrl & LCDCTRL_LCD_ON) == 0 )
		gpu_blank_gb_surface();

	return 0;
}
