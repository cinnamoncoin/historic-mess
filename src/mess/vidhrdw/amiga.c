/***************************************************************************
Commodore Amiga - (c) 1985, Commodore Bussines Machines Co.

Preliminary driver by:

Ernesto Corvi
ernesto@imagina.com

***************************************************************************/

#include "driver.h"
#include "vidhrdw/generic.h"
#include "mess/systems/amiga.h"

extern custom_regs_def custom_regs;

struct update_regs_def {
/* display window */
	int v_start;
	int v_stop;
	int h_start;
	int h_stop;
	int old_DIWSTRT;
	int old_DIWSTOP;
/* display data fetch */
	int ddf_start_pixel;
	int ddf_word_count;
	int old_DDFSTRT;
/* update time variables */
	unsigned short pixel_word[MAX_PLANES];
	int	current_bit;
	int fetch_pending;
	int fetch_count;
	unsigned short back_color;
	int old_COLOR0;
	unsigned char *RAM;
};

static struct update_regs_def update_regs;


/***************************************************************************

	Copper emulation

***************************************************************************/

#define COPPER_COLOR_CLOCKS_PER_INST 4

typedef struct {
	unsigned long pc;
	int waiting;
	int wait_v_pos;
	int wait_h_pos;
	int waitforblit;
} copper_def;

static copper_def copper;

void copper_setpc( unsigned long pc ) {
	copper.pc = pc;
	copper.waiting = 0;
}

void copper_reset( void ) {
	copper.pc = ( custom_regs.COPLCH[0] << 16 ) | custom_regs.COPLCL[0];
	copper.waiting = 0;
}

int copper_update( int x_pos, int y_pos, int *end_x ) {
	int i, inst, param;

	/*********************************************************************************

		The Copper can only read the lower 8 bits of the vertical counter. To access
		the next 6 lines, you need to wait for line 255, and then you can check from
		lines 0 to 6 (corresponding to 256 to 261) to do things before it wraps again.

	 ********************************************************************************/

	y_pos &= 0xff;

	/* convert the x coordinate to color clocks coordinate */
	x_pos = ( x_pos >> 1 );

	/* 56.794 instructions per line max (280 ns per inst) */
	for ( i = 0; i < 57; i++ ) {

		/* see if we are in a waiting state */
		if ( copper.waiting ) {
			if ( y_pos < copper.wait_v_pos ) { /* are we in the right scanline yet? */
				return 1; /* we arent, render the whole line as is */
			}

			if ( y_pos == copper.wait_v_pos ) {

				if ( x_pos < copper.wait_h_pos ) {
					/* see if we can ever reach it, otherwise, finish the line */
					if ( copper.wait_h_pos > ( ( Machine->drv->screen_width - 1 ) >> 1 ) )
						return 1;

					*end_x = ( copper.wait_h_pos << 1 );
					return 0;
				}
			}

			/* if we're told to wait for the blitter too, check for it now */
			if ( copper.waitforblit )
			{
				if ( custom_regs.DMACON & DMACON_BBUSY )
					return 1;
				else
					copper.waitforblit = 0;
			}

			copper.waiting = 0;
			copper.pc += 4;
		}

		inst = READ_WORD( &(update_regs.RAM[copper.pc]) );
		param = READ_WORD( &(update_regs.RAM[copper.pc + 2]) );

		if ( !( inst & 1 ) ) { /* MOVE instruction */
			int min = 0x80 - ( custom_regs.COPCON << 5 );

			inst &= 0x1fe;

			copper.pc += 4;

			if ( inst >= min )	/* If not invalid, go at it */
				amiga_custom_w( inst, param );
			else {
				/* stop the copper until the next frame */
				copper.waiting = 1;
				copper.wait_v_pos = 0xff;
				copper.wait_h_pos = 0xfe;
				return 1;
			}

			x_pos += COPPER_COLOR_CLOCKS_PER_INST;
			continue;
		}

		if ( !( param & 1 ) ) { /* WAIT instruction */
			copper.waiting = 1;
			copper.wait_v_pos = ( inst >> 8 ) & 0xff;
			copper.wait_v_pos &= ( ( param >> 8 ) & 0x7f ) | 0x80;
			copper.wait_h_pos = ( inst & 0xfe );
			copper.wait_h_pos &= ( param & 0xfe );
			copper.waitforblit = !( ( param >> 15 ) & 1 );
			x_pos += COPPER_COLOR_CLOCKS_PER_INST;
			continue;
		}

		if ( ( inst & 1 ) && ( param & 1 ) ) { /* SKIP instruction */
			int skip_v_pos, skip_h_pos, skip_waitforblit;

			skip_v_pos = ( inst >> 8 ) & 0xff;
			skip_v_pos &= ( param >> 8 ) & 0x7f;
			skip_h_pos = ( inst & 0xfe );
			skip_h_pos &= ( param & 0xfe );
			skip_waitforblit = !( ( param >> 15 ) & 1 );

			if ( y_pos < skip_v_pos ) {
				copper.pc += 4;
				x_pos += COPPER_COLOR_CLOCKS_PER_INST;
				continue;
			}

			if ( y_pos == skip_v_pos ) {
				if ( x_pos < skip_h_pos ) {
					copper.pc += 4;
					x_pos += COPPER_COLOR_CLOCKS_PER_INST;
					continue;
				}
			}

			if ( skip_waitforblit ) {
				if ( custom_regs.DMACON & DMACON_BBUSY ) {
					copper.pc += 4;
					x_pos += COPPER_COLOR_CLOCKS_PER_INST;
					continue;
				}
			}

			copper.pc += 8;
			x_pos += COPPER_COLOR_CLOCKS_PER_INST;
			continue;
		}

		/* if we got here, we're stuck in a illegal copper instruction */
		if ( errorlog )
			fprintf( errorlog, "This program attempted to execute an illegal copper instruction!\n" );

		/* stop the copper until the next frame */
		copper.waiting = 1;
		copper.wait_v_pos = 0xff;
		copper.wait_h_pos = 0xfe;

		return 1;
	}

	/* if we got here, we ran enough instructions for this line, thus we complete it */
	return 1;
}

void update_modulos ( int planes ) {
	int i;

	for ( i = 0; i < planes; i++ )
		if ( i & 1 )
			custom_regs.BPLPTR[i] += custom_regs.BPL2MOD;
		else
			custom_regs.BPLPTR[i] += custom_regs.BPL1MOD;
}

void init_update_regs( void ) {
	int	ddf_color_clocks_offs, ddf_res_offs;

	if ( update_regs.old_DIWSTRT != custom_regs.DIWSTRT ) {
		/* display window */
		update_regs.v_start = ( custom_regs.DIWSTRT >> 8 ) & 0xff;
		update_regs.h_start	= custom_regs.DIWSTRT & 0xff;
		update_regs.old_DIWSTRT = custom_regs.DIWSTRT;
	}

	if ( update_regs.old_DIWSTOP != custom_regs.DIWSTOP ) {
		/* display window */
		update_regs.v_stop = ( custom_regs.DIWSTOP >> 8 ) & 0xff;
		update_regs.v_stop |= ( ( update_regs.v_stop << 1 ) ^ 0x0100 ) & 0x0100; /* bit 8 = !bit 7 */
		update_regs.h_stop = ( custom_regs.DIWSTOP & 0xff ) | 0x0100;
		update_regs.old_DIWSTOP = custom_regs.DIWSTOP;
	}

	if ( update_regs.old_DDFSTRT != custom_regs.DDFSTRT ) {
		update_regs.fetch_pending = 1;
		update_regs.fetch_count = 0;
		update_regs.old_DDFSTRT = custom_regs.DDFSTRT;
	}

	/* display data fetch */
	if ( custom_regs.BPLCON0 & BPLCON0_HIRES ) {
		ddf_color_clocks_offs = 9; /* 4.5 * 2 in hi res */
		ddf_res_offs = 2;
	} else {
		ddf_color_clocks_offs = 17; /* 8.5 * 2 in lo res */
		ddf_res_offs = 3;
	}

	update_regs.ddf_start_pixel = ( custom_regs.DDFSTRT << 1 ) + ddf_color_clocks_offs;
	update_regs.ddf_word_count = ( -( custom_regs.DDFSTRT - custom_regs.DDFSTOP - 12 ) ) >> ddf_res_offs;

	if ( update_regs.old_COLOR0 != custom_regs.COLOR[0] ) {
		update_regs.back_color = Machine->pens[custom_regs.COLOR[0]];
		update_regs.old_COLOR0 = custom_regs.COLOR[0];
	}
}

void render_pixel( unsigned short *dst, int planes, int x, int y, int min_x ) {
	int i, color;

	/*********************************************************************************
		Now, we're inside the possible data fetch area, note that data fetch can start
		before the display window horizontal start, but the pixels wont be shown.
		However, we	still have to emulate this properly, so scrolling effects and
		modulos can be added properly.
	 ********************************************************************************/

	if ( x < update_regs.ddf_start_pixel ) { /* see if we need to start fetching */
		dst[x] = update_regs.back_color; /* fill the pixel with color 0 */
		return;
	} else {
		/* Now we have to figure out if we need to fetch another 16 bit word */
		if ( update_regs.fetch_pending ) {
			/* see if we are past DDFSTOP */
			if ( ++update_regs.fetch_count > update_regs.ddf_word_count ) {
				dst[x] = update_regs.back_color; /* fill the pixel with color 0 */
				return;
			}

			/* fetch the new word from the bitplane pointers, and update them */
			for ( i = 0; i < planes; i++ ) {
				update_regs.pixel_word[i] = READ_WORD( &(update_regs.RAM[custom_regs.BPLPTR[i]]) );
				custom_regs.BPLPTR[i] += 2;
			}

			update_regs.current_bit = 15;
			update_regs.fetch_pending = 0;
		}

		if ( x >= min_x ) {
			/* before rendering the pixel, see if we still are within display window bounds */
			if ( x > update_regs.h_stop ) {
				dst[x] = update_regs.back_color;
			} else {
				/* now we're ready to render it */
				color = 0;
				for ( i = 0; i < planes; i++ ) {
					color |= ( ( ( update_regs.pixel_word[i] ) >> update_regs.current_bit ) & 1 ) << i;
				}

				dst[x] = Machine->pens[custom_regs.COLOR[color]];
			}
		}

		/* temporary HACK(tm) to display hi-res */
		if ( custom_regs.BPLCON0 & BPLCON0_HIRES )
			update_regs.current_bit -= 2;
		else
			update_regs.current_bit--;

		/* see if we're done with this word */
		if ( update_regs.current_bit < 0 )
			update_regs.fetch_pending = 1; /* signal we need a new fetch */
	}
}

void amiga_vh_screenrefresh( struct osd_bitmap *bitmap, int full_refresh ) {
	int planes = 0, sw = Machine->drv->screen_width;
	int min_x = Machine->drv->visible_area.min_x;
	int y, x, start_x, end_x, line_done;
	unsigned short *dst;

	/* reset the copper */
	copper_reset();

	/* normalize some register values */
	if ( custom_regs.DDFSTOP < custom_regs.DDFSTRT )
		custom_regs.DDFSTOP = custom_regs.DDFSTRT;

	/* init cached data */
	update_regs.old_COLOR0 = -1;
	update_regs.old_DIWSTRT = -1;
	update_regs.old_DIWSTOP = -1;
	update_regs.old_DDFSTRT = -1;
	update_regs.RAM = Machine->memory_region[0];

	for ( y = 0; y < Machine->drv->screen_height; y++ ) {
		int bitplane_dma_disabled;

		/* start of a new line, signal we're not done with it and fill up vars */
		line_done = 0;
		start_x = 0;
		dst = ( unsigned short * )bitmap->line[y];

		update_regs.fetch_pending = 1;
		update_regs.fetch_count = 0;

		/* make sure we complete the line */
		do {

			/* start of a new line... check if the copper is (still) enabled */
			if ( ( custom_regs.DMACON & ( DMACON_COPEN | DMACON_DMAEN ) ) == ( DMACON_COPEN | DMACON_DMAEN ) )
				line_done = copper_update( start_x, y, &end_x );
			else /* if the copper is not on, then complete the rest of the line */
				line_done = 1;

			if ( line_done )
				end_x = sw;

			/* precaulculate some update registers */
			init_update_regs();

			/* get the number of planes */
			planes = ( custom_regs.BPLCON0 & ( BPLCON0_BPU0 | BPLCON0_BPU1 | BPLCON0_BPU2 ) ) >> 12;

			/* precalculate if the bitplane dma is enabled */
			bitplane_dma_disabled = ( custom_regs.DMACON & ( DMACON_BPLEN | DMACON_DMAEN ) ) != ( DMACON_BPLEN | DMACON_DMAEN );

			/***************************************************************************
				First we check for a number of conditions to see if we can render image
				pixels yet. Otherwise, we fill with the background color.
			 **************************************************************************/

			if ( bitplane_dma_disabled || planes == 0 || y < update_regs.v_start || y >= update_regs.v_stop ) {
				for ( x = start_x; x < end_x; x++ )
					dst[x] = update_regs.back_color;
			} else { /* render the pixels (inlined for readability ) */
				for ( x = start_x; x < end_x; x++ )
					render_pixel( dst, planes, x, y, min_x );
			}

			/* now we start from where we left off */
			start_x = end_x;

		} while ( !line_done );

		/* update modulos */
		if ( !bitplane_dma_disabled )
			update_modulos( planes );
	}
}

void amiga_init_palette(unsigned char *palette, unsigned short *colortable,const unsigned char *color_prom) {
	int i;

	for ( i = 0; i < 0x1000; i++ ) {
		int r, g, b;

		r = ( i >> 8 ) & 0x0f;
		g = ( i >> 4 ) & 0x0f;
		b = i & 0x0f;

		r = ( r << 4 ) | ( r );
		g = ( g << 4 ) | ( g );
		b = ( b << 4 ) | ( b );

		*palette++ = r;
		*palette++ = g;
		*palette++ = b;

		colortable[i] = i;
	}
}

int amiga_vh_start( void ) {
	return 0;
}

void amiga_vh_stop( void ) {
}
