/***************************************************************************

  $Id: pc8801.c,v 1.51 2007/01/19 03:28:56 npwoods Exp $

***************************************************************************/

#include "driver.h"
#include "inputx.h"
#include "cpu/z80/z80.h"
#include "video/generic.h"
#include "includes/pc8801.h"
#include "machine/nec765.h"
#include "machine/8255ppi.h"
#include "includes/d88.h"
#include "devices/basicdsk.h"
#include "sound/2203intf.h"

static gfx_layout char_layout_40L_h =
{
	8, 4,						/* 16 x 4 graphics */
	1024,						/* 256 codes */
	1,							/* 1 bit per pixel */
	{ 0x1000*8 },				/* no bitplanes */
	{ 0, 0, 1, 1, 2, 2, 3, 3 },
	{ 0*8, 0*8, 1*8, 1*8 },
	8 * 2						/* code takes 8 times 8 bits */
};

static gfx_layout char_layout_40R_h =
{
	8, 4,						/* 16 x 4 graphics */
	1024,						/* 256 codes */
	1,							/* 1 bit per pixel */
	{ 0x1000*8 },				/* no bitplanes */
	{ 4, 4, 5, 5, 6, 6, 7, 7 },
	{ 0*8, 0*8, 1*8, 1*8 },
	8 * 2						/* code takes 8 times 8 bits */
};

static gfx_layout char_layout_80_h =
{
	8, 4,           /* 16 x 4 graphics */
	1024,            /* 256 codes */
	1,                      /* 1 bit per pixel */
	{ 0x1000*8 },          /* no bitplanes */
	{ 0, 1, 2, 3, 4, 5, 6, 7 },
	{ 0*8, 0*8, 1*8, 1*8 },
	8 * 2           /* code takes 8 times 8 bits */
};

static gfx_layout char_layout_40L_l =
{
	8, 2,           /* 16 x 4 graphics */
	1024,            /* 256 codes */
	1,                      /* 1 bit per pixel */
	{ 0x1000*8 },          /* no bitplanes */
	{ 0, 0, 1, 1, 2, 2, 3, 3 },
	{ 0*8, 1*8 },
	8 * 2           /* code takes 8 times 8 bits */
};

static gfx_layout char_layout_40R_l =
{
	8, 2,           /* 16 x 4 graphics */
	1024,            /* 256 codes */
	1,                      /* 1 bit per pixel */
	{ 0x1000*8 },          /* no bitplanes */
	{ 4, 4, 5, 5, 6, 6, 7, 7 },
	{ 0*8, 1*8 },
	8 * 2           /* code takes 8 times 8 bits */
};

static gfx_layout char_layout_80_l =
{
	8, 2,           /* 16 x 4 graphics */
	1024,            /* 256 codes */
	1,                      /* 1 bit per pixel */
	{ 0x1000*8 },          /* no bitplanes */
	{ 0, 1, 2, 3, 4, 5, 6, 7 },
	{ 0*8, 1*8 },
	8 * 2           /* code takes 8 times 8 bits */
};

static gfx_decode gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0, &char_layout_80_l, 0, 16 },
	{ REGION_GFX1, 0, &char_layout_40L_l, 0, 16 },
	{ REGION_GFX1, 0, &char_layout_40R_l, 0, 16 },
	{ REGION_GFX1, 0, &char_layout_80_h, 0, 16 },
	{ REGION_GFX1, 0, &char_layout_40L_h, 0, 16 },
	{ REGION_GFX1, 0, &char_layout_40R_h, 0, 16 },
	{-1}
};       /* end of array */

/* Macro for DIPSW-1 */
#define DIPSW_1_1 \
	PORT_DIPNAME( 0x01, 0x01, "Terminal mode" ) \
	PORT_DIPSETTING(    0x01, DEF_STR( Off ) ) \
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
#define DIPSW_1_2 \
	PORT_DIPNAME( 0x02, 0x00, "Text width" ) \
	PORT_DIPSETTING(    0x02, "40 chars/line" ) \
	PORT_DIPSETTING(    0x00, "80 chars/line" )
#define DIPSW_1_3 \
	PORT_DIPNAME( 0x04, 0x00, "Text height" ) \
	PORT_DIPSETTING(    0x04, "20 lines/screen" ) \
	PORT_DIPSETTING(    0x00, "25 lines/screen" )
#define DIPSW_1_4 \
	PORT_DIPNAME( 0x08, 0x08, "Enable S parameter" ) \
	PORT_DIPSETTING(    0x08, DEF_STR( Off ) ) \
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
#define DIPSW_1_5 \
	PORT_DIPNAME( 0x10, 0x00, "Enable DEL code" ) \
	PORT_DIPSETTING(    0x10, DEF_STR( Off ) ) \
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
#define DIPSW_1_6 \
	PORT_DIPNAME( 0x20, 0x20, "Memory wait" ) \
	PORT_DIPSETTING(    0x20, DEF_STR( Off ) ) \
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
#define DIPSW_1_7 \
	PORT_DIPNAME( 0x40, 0x40, "Disable CMD SING" ) \
	PORT_DIPSETTING(    0x40, DEF_STR( Off ) ) \
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )

/* Macro for DIPSW-2 */
#define DIPSW_2_1 \
	PORT_DIPNAME( 0x01, 0x01, "Parity generate" ) \
	PORT_DIPSETTING(    0x01, DEF_STR( Off ) ) \
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
#define DIPSW_2_2 \
	PORT_DIPNAME( 0x02, 0x00, "Parity type" ) \
	PORT_DIPSETTING(    0x00, "Even" ) \
	PORT_DIPSETTING(    0x02, "Odd" )
#define DIPSW_2_3 \
	PORT_DIPNAME( 0x04, 0x00, "Serial character length" ) \
	PORT_DIPSETTING(    0x04, "7 bits/char" ) \
	PORT_DIPSETTING(    0x00, "8 bits/char" )
#define DIPSW_2_4 \
	PORT_DIPNAME( 0x08, 0x08, "Stop bit length" ) \
	PORT_DIPSETTING(    0x08, "1" ) \
	PORT_DIPSETTING(    0x00, "2" )
#define DIPSW_2_5 \
	PORT_DIPNAME( 0x10, 0x10, "Enable X parameter" ) \
	PORT_DIPSETTING(    0x10, DEF_STR( Off ) ) \
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
#define DIPSW_2_6 \
	PORT_DIPNAME( 0x20, 0x20, "Duplex mode" ) \
	PORT_DIPSETTING(    0x20, "half duplex" ) \
	PORT_DIPSETTING(    0x00, "full duplex" )
#define DIPSW_2_7 \
	PORT_DIPNAME( 0x40, 0x00, "Boot from internal FD" ) \
	PORT_DIPSETTING(    0x10, DEF_STR( Off ) ) \
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
#define DIPSW_2_8 \
	PORT_DIPNAME( 0x80, 0x80, "Disable internal FD" ) \
	PORT_DIPSETTING(    0x80, DEF_STR( Off ) ) \
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )

/* Macro for other switch */
#define SW_V1V2N \
	PORT_DIPNAME( 0x03, 0x01, "Basic mode" ) \
	PORT_DIPSETTING(    0x02, "N-BASIC" ) \
	PORT_DIPSETTING(    0x03, "N88-BASIC (V1)" ) \
	PORT_DIPSETTING(    0x01, "N88-BASIC (V2)" )

#define SW_HS \
	PORT_DIPNAME( 0x04, 0x04, "Speed mode" ) \
	PORT_DIPSETTING(    0x00, "Slow" ) \
	PORT_DIPSETTING(    0x04, DEF_STR( High ) )

#define SW_8MHZ \
	PORT_DIPNAME( 0x08, 0x00, "Main CPU clock" ) \
	PORT_DIPSETTING(    0x00, "4MHz" ) \
	PORT_DIPSETTING(    0x08, "8MHz" )

#define SW_4MHZ_ONLY \
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_UNUSED )

#define SW_SERIAL \
	PORT_DIPNAME( 0xf0, 0x80, "Serial speed" ) \
	PORT_DIPSETTING(    0x10, "75bps" ) \
	PORT_DIPSETTING(    0x20, "150bps" ) \
	PORT_DIPSETTING(    0x30, "300bps" ) \
	PORT_DIPSETTING(    0x40, "600bps" ) \
	PORT_DIPSETTING(    0x50, "1200bps" ) \
	PORT_DIPSETTING(    0x60, "2400bps" ) \
	PORT_DIPSETTING(    0x70, "4800bps" ) \
	PORT_DIPSETTING(    0x80, "9600bps" ) \
	PORT_DIPSETTING(    0x90, "19200bps" )

#define SW_EXTMEM \
	PORT_DIPNAME( 0x1f, 0x00, "Extension memory" ) \
	PORT_DIPSETTING(    0x00, DEF_STR( None ) ) \
	PORT_DIPSETTING(    0x01, "32KB (PC-8012-02 x 1)" ) \
	PORT_DIPSETTING(    0x02, "64KB (PC-8012-02 x 2)" ) \
	PORT_DIPSETTING(    0x03, "128KB (PC-8012-02 x 4)" ) \
	PORT_DIPSETTING(    0x04, "128KB (PC-8801-02N x 1)" ) \
	PORT_DIPSETTING(    0x05, "256KB (PC-8801-02N x 2)" ) \
	PORT_DIPSETTING(    0x06, "512KB (PC-8801-02N x 4)" ) \
	PORT_DIPSETTING(    0x07, "1M (PIO-8234H-1M x 1)" ) \
	PORT_DIPSETTING(    0x08, "2M (PIO-8234H-2M x 1)" ) \
	PORT_DIPSETTING(    0x09, "4M (PIO-8234H-2M x 2)" ) \
	PORT_DIPSETTING(    0x0a, "8M (PIO-8234H-2M x 4)" ) \
	PORT_DIPSETTING(    0x0b, "1.1M (PIO-8234H-1M x 1 + PC-8801-02N x 1)" ) \
	PORT_DIPSETTING(    0x0c, "2.1M (PIO-8234H-2M x 1 + PC-8801-02N x 1)" ) \
	PORT_DIPSETTING(    0x0d, "4.1M (PIO-8234H-2M x 2 + PC-8801-02N x 1)" )

#define DUMMY_ROW \
	PORT_START \
	PORT_BIT( 0xff, IP_ACTIVE_LOW, IPT_UNUSED )



INPUT_PORTS_START( pc88sr )
	/* [0] */
	PORT_START
	PORT_BIT (0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("pad 0") PORT_CODE(KEYCODE_0_PAD) PORT_CHAR(UCHAR_MAMEKEY(0_PAD))
	PORT_BIT (0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("pad 1") PORT_CODE(KEYCODE_1_PAD) PORT_CHAR(UCHAR_MAMEKEY(1_PAD))
	PORT_BIT (0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("pad 2") PORT_CODE(KEYCODE_2_PAD) PORT_CHAR(UCHAR_MAMEKEY(2_PAD))
	PORT_BIT (0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("pad 3") PORT_CODE(KEYCODE_3_PAD) PORT_CHAR(UCHAR_MAMEKEY(3_PAD))
	PORT_BIT (0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("pad 4") PORT_CODE(KEYCODE_4_PAD) PORT_CHAR(UCHAR_MAMEKEY(4_PAD))
	PORT_BIT (0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("pad 5") PORT_CODE(KEYCODE_5_PAD) PORT_CHAR(UCHAR_MAMEKEY(5_PAD))
	PORT_BIT (0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("pad 6") PORT_CODE(KEYCODE_6_PAD) PORT_CHAR(UCHAR_MAMEKEY(6_PAD))
	PORT_BIT (0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("pad 7") PORT_CODE(KEYCODE_7_PAD) PORT_CHAR(UCHAR_MAMEKEY(7_PAD))

	/* [1] */
	PORT_START
	PORT_BIT (0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("pad 8") PORT_CODE(KEYCODE_8_PAD) PORT_CHAR(UCHAR_MAMEKEY(8_PAD))
	PORT_BIT (0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("pad 9") PORT_CODE(KEYCODE_9_PAD) PORT_CHAR(UCHAR_MAMEKEY(9_PAD))
	PORT_BIT (0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("pad *") PORT_CODE(KEYCODE_ASTERISK) PORT_CHAR(UCHAR_MAMEKEY(ASTERISK))
	PORT_BIT (0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("pad +") PORT_CODE(KEYCODE_PLUS_PAD) PORT_CHAR(UCHAR_MAMEKEY(PLUS_PAD))
	PORT_BIT (0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("pad =") PORT_CODE(KEYCODE_END) PORT_CHAR(UCHAR_MAMEKEY(END)) /* BAD */
	PORT_BIT (0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("pad ,") PORT_CODE(KEYCODE_NUMLOCK) PORT_CHAR(UCHAR_MAMEKEY(NUMLOCK)) /* BAD */
	PORT_BIT (0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("pad .") PORT_CODE(KEYCODE_DEL_PAD) PORT_CHAR(UCHAR_MAMEKEY(DEL_PAD))
	PORT_BIT (0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("pad return") PORT_CODE(KEYCODE_ENTER) PORT_CODE(KEYCODE_ENTER_PAD) PORT_CHAR(13)

	/* [2] */
	PORT_START
	PORT_BIT (0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_OPENBRACE) PORT_CHAR('@') PORT_CHAR('~')
	PORT_BIT (0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_A) PORT_CHAR('a') PORT_CHAR('A')
	PORT_BIT (0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_B) PORT_CHAR('b') PORT_CHAR('B')
	PORT_BIT (0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_C) PORT_CHAR('c') PORT_CHAR('C')
	PORT_BIT (0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_D) PORT_CHAR('d') PORT_CHAR('D')
	PORT_BIT (0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_E) PORT_CHAR('e') PORT_CHAR('E')
	PORT_BIT (0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_F) PORT_CHAR('f') PORT_CHAR('F')
	PORT_BIT (0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_G) PORT_CHAR('g') PORT_CHAR('G')

	/* [3] */
	PORT_START
	PORT_BIT (0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_H) PORT_CHAR('h') PORT_CHAR('H')
	PORT_BIT (0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_I) PORT_CHAR('i') PORT_CHAR('I')
	PORT_BIT (0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_J) PORT_CHAR('j') PORT_CHAR('J')
	PORT_BIT (0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_K) PORT_CHAR('k') PORT_CHAR('K')
	PORT_BIT (0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_L) PORT_CHAR('l') PORT_CHAR('L')
	PORT_BIT (0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_M) PORT_CHAR('m') PORT_CHAR('M')
	PORT_BIT (0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_N) PORT_CHAR('n') PORT_CHAR('N')
	PORT_BIT (0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_O) PORT_CHAR('o') PORT_CHAR('O')

	/* [4] */
	PORT_START
	PORT_BIT (0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_P) PORT_CHAR('p') PORT_CHAR('P')
	PORT_BIT (0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_Q) PORT_CHAR('q') PORT_CHAR('Q')
	PORT_BIT (0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_R) PORT_CHAR('r') PORT_CHAR('R')
	PORT_BIT (0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_S) PORT_CHAR('s') PORT_CHAR('S')
	PORT_BIT (0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_T) PORT_CHAR('t') PORT_CHAR('T')
	PORT_BIT (0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_U) PORT_CHAR('u') PORT_CHAR('U')
	PORT_BIT (0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_V) PORT_CHAR('v') PORT_CHAR('V')
	PORT_BIT (0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_W) PORT_CHAR('w') PORT_CHAR('W')

	/* [5] */
	PORT_START
	PORT_BIT (0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_X) PORT_CHAR('x') PORT_CHAR('X')
	PORT_BIT (0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_Y) PORT_CHAR('y') PORT_CHAR('Y')
	PORT_BIT (0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_Z) PORT_CHAR('z') PORT_CHAR('Z')
	PORT_BIT (0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_CLOSEBRACE) PORT_CHAR('[') PORT_CHAR('{')
	PORT_BIT (0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_BACKSLASH2) PORT_CHAR('\\') PORT_CHAR('|')
	PORT_BIT (0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_BACKSLASH) PORT_CHAR(']') PORT_CHAR('}')
	PORT_BIT (0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_EQUALS) PORT_CHAR('^') PORT_CHAR('~')
	PORT_BIT (0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_MINUS) PORT_CHAR('-') PORT_CHAR('=')

	/* [6] */
	PORT_START
	PORT_BIT (0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_0) PORT_CHAR('0')
	PORT_BIT (0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_1) PORT_CHAR('1') PORT_CHAR('!')
	PORT_BIT (0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_2) PORT_CHAR('2') PORT_CHAR('\"')
	PORT_BIT (0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_3) PORT_CHAR('3') PORT_CHAR('#')
	PORT_BIT (0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_4) PORT_CHAR('4') PORT_CHAR('$')
	PORT_BIT (0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_5) PORT_CHAR('5') PORT_CHAR('%')
	PORT_BIT (0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_6) PORT_CHAR('6') PORT_CHAR('&')
	PORT_BIT (0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_7) PORT_CHAR('7') PORT_CHAR('\'')

	/* [7] */
	PORT_START
	PORT_BIT (0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_8) PORT_CHAR('8') PORT_CHAR('(')
	PORT_BIT (0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_9) PORT_CHAR('9') PORT_CHAR(')')
	PORT_BIT (0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_QUOTE) PORT_CHAR(':') PORT_CHAR('*')
	PORT_BIT (0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_COLON) PORT_CHAR(';') PORT_CHAR('+')
	PORT_BIT (0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_COMMA) PORT_CHAR(',') PORT_CHAR('<')
	PORT_BIT (0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_STOP) PORT_CHAR('.') PORT_CHAR('>')
	PORT_BIT (0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_SLASH) PORT_CHAR('/') PORT_CHAR('?')
	PORT_BIT (0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("  _") PORT_CODE(KEYCODE_END) PORT_CHAR(0) PORT_CHAR('_')

	/* [8] */
	PORT_START
	PORT_BIT (0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("HOME/CLR") PORT_CODE(KEYCODE_HOME)
	PORT_BIT (0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("UP") PORT_CODE(KEYCODE_UP) PORT_CHAR(UCHAR_MAMEKEY(UP))
	PORT_BIT (0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("RIGHT") PORT_CODE(KEYCODE_RIGHT) PORT_CHAR(UCHAR_MAMEKEY(RIGHT))
	PORT_BIT (0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("INS/DEL") PORT_CODE(KEYCODE_DEL) PORT_CODE(KEYCODE_INSERT) PORT_CHAR(8) PORT_CHAR(UCHAR_MAMEKEY(INSERT))
	PORT_BIT (0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("GRPH") PORT_CODE(KEYCODE_LALT) PORT_CODE(KEYCODE_RALT)
	PORT_BIT (0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("KANA") PORT_CODE(KEYCODE_SCRLOCK) /* BAD */ PORT_TOGGLE
	PORT_BIT (0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("SHIFT") PORT_CODE(KEYCODE_LSHIFT) PORT_CODE(KEYCODE_RSHIFT) PORT_CHAR(UCHAR_SHIFT_1)
	PORT_BIT (0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("CTRL") PORT_CODE(KEYCODE_LCONTROL) PORT_CODE(KEYCODE_RCONTROL)

	/* [9] */
	PORT_START
	PORT_BIT (0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("STOP") PORT_CODE(KEYCODE_PAUSE)
	PORT_BIT (0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("F1") PORT_CODE(KEYCODE_F1) PORT_CHAR(UCHAR_MAMEKEY(F1))
	PORT_BIT (0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("F2") PORT_CODE(KEYCODE_F2) PORT_CHAR(UCHAR_MAMEKEY(F2))
	PORT_BIT (0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("F3") PORT_CODE(KEYCODE_F3) /* BAD */ PORT_CHAR(UCHAR_MAMEKEY(F3))
	PORT_BIT (0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("F4") PORT_CODE(KEYCODE_F4) /* BAD */ PORT_CHAR(UCHAR_MAMEKEY(F4))
	PORT_BIT (0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("F5") PORT_CODE(KEYCODE_F5) PORT_CHAR(UCHAR_MAMEKEY(F5))
	PORT_BIT (0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("SPACE") PORT_CODE(KEYCODE_SPACE) PORT_CHAR(' ')
	PORT_BIT (0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("ESC") PORT_CODE(KEYCODE_ESC) PORT_CHAR(27 /* BAD */)

	/* [10] */
	PORT_START
	PORT_BIT (0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("TAB") PORT_CODE(KEYCODE_TAB) PORT_CHAR(9 /* BAD */)
	PORT_BIT (0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("DOWN") PORT_CODE(KEYCODE_DOWN) PORT_CHAR(UCHAR_MAMEKEY(DOWN))
	PORT_BIT (0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("LEFT") PORT_CODE(KEYCODE_LEFT) PORT_CHAR(UCHAR_MAMEKEY(LEFT))
	PORT_BIT (0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("HELP") PORT_CODE(KEYCODE_END) PORT_CODE(CODE_NONE /* BAD? */)
	PORT_BIT (0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("COPY") PORT_CODE(KEYCODE_PRTSCR) PORT_CODE(CODE_NONE /* BAD? */)
	PORT_BIT (0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("-") PORT_CODE(KEYCODE_MINUS_PAD) PORT_CHAR('-')
	PORT_BIT (0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("/") PORT_CODE(KEYCODE_SLASH_PAD) PORT_CHAR('/')
	PORT_BIT (0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("CAPS") PORT_CODE(KEYCODE_CAPSLOCK) PORT_CODE(CODE_NONE /* BAD? */) PORT_TOGGLE

	/* [11] */
	PORT_START
	PORT_BIT (0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("ROLL UP") PORT_CODE(KEYCODE_PGUP) PORT_CODE(CODE_NONE /* BAD? */) PORT_CHAR(UCHAR_MAMEKEY(PGUP))
	PORT_BIT (0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("ROLL DOWN") PORT_CODE(KEYCODE_PGDN) PORT_CODE(CODE_NONE /* BAD? */) PORT_CHAR(UCHAR_MAMEKEY(PGDN))
	PORT_BIT( 0xfc, IP_ACTIVE_LOW, IPT_UNUSED )

	DUMMY_ROW	/* port 0x0c */ \
	DUMMY_ROW	/* port 0x0d */ \
	DUMMY_ROW	/* port 0x0e */ \
	DUMMY_ROW	/* port 0x0f */

	PORT_START		/* EXSWITCH */
	SW_V1V2N
	SW_HS
	SW_4MHZ_ONLY
	SW_SERIAL

	PORT_START		/* DIP-SW1 */
	DIPSW_1_1
	DIPSW_1_2
	DIPSW_1_3
	DIPSW_1_4
	DIPSW_1_5
	DIPSW_1_6
	DIPSW_1_7

	PORT_START		/* DIP-SW2 */
	DIPSW_2_1
	DIPSW_2_2
	DIPSW_2_3
	DIPSW_2_4
	DIPSW_2_5
	DIPSW_2_6
	DIPSW_2_7
	DIPSW_2_8

	PORT_START		/* extension memory setting */
	SW_EXTMEM

INPUT_PORTS_END


ADDRESS_MAP_START( pc8801_mem , ADDRESS_SPACE_PROGRAM, 8)
    AM_RANGE(0x0000, 0x5fff) AM_RAMBANK(1)
    AM_RANGE(0x6000, 0x7fff) AM_RAMBANK(2)
    AM_RANGE(0x8000, 0x83ff) AM_RAMBANK(3)
    AM_RANGE(0x8400, 0xbfff) AM_RAMBANK(4)
    AM_RANGE(0xc000, 0xefff) AM_RAMBANK(5)
    AM_RANGE(0xf000, 0xffff) AM_RAMBANK(6)
ADDRESS_MAP_END

ADDRESS_MAP_START( pc88sr_io, ADDRESS_SPACE_IO, 8)
	ADDRESS_MAP_FLAGS( AMEF_ABITS(8) ) 
	ADDRESS_MAP_FLAGS( AMEF_UNMAP(1) )
	AM_RANGE(0x00, 0x00) AM_READ( input_port_0_r )  /* keyboard */
	AM_RANGE(0x01, 0x01) AM_READ( input_port_1_r )  /* keyboard */
	AM_RANGE(0x02, 0x02) AM_READ( input_port_2_r )  /* keyboard */
	AM_RANGE(0x03, 0x03) AM_READ( input_port_3_r )  /* keyboard */
	AM_RANGE(0x04, 0x04) AM_READ( input_port_4_r )  /* keyboard */
	AM_RANGE(0x05, 0x05) AM_READ( input_port_5_r )  /* keyboard */
	AM_RANGE(0x06, 0x06) AM_READ( input_port_6_r )  /* keyboard */
	AM_RANGE(0x07, 0x07) AM_READ( input_port_7_r )  /* keyboard */
	AM_RANGE(0x08, 0x08) AM_READ( input_port_8_r )  /* keyboard */
	AM_RANGE(0x09, 0x09) AM_READ( input_port_9_r )  /* keyboard */
	AM_RANGE(0x0a, 0x0a) AM_READ( input_port_10_r ) /* keyboard */
	AM_RANGE(0x0b, 0x0b) AM_READ( input_port_11_r ) /* keyboard */
	AM_RANGE(0x0c, 0x0c) AM_READ( input_port_12_r ) /* keyboard */
	AM_RANGE(0x0d, 0x0d) AM_READ( input_port_13_r ) /* keyboard */
	AM_RANGE(0x0e, 0x0e) AM_READ( input_port_14_r ) /* keyboard */
	AM_RANGE(0x0f, 0x0f) AM_READ( input_port_15_r ) /* keyboard */
	AM_RANGE(0x10, 0x10) AM_WRITE( pc8801_calender ) /* printer and clock and UOP3 */
	AM_RANGE(0x20, 0x21) AM_NOP	/* RS-232C and cassette (not yet) */
	AM_RANGE(0x30, 0x30) AM_READWRITE( pc88sr_inport_30, pc88sr_outport_30 ) /* DIP-SW1 */
	AM_RANGE(0x31, 0x31) AM_READWRITE( pc88sr_inport_31, pc88sr_outport_31 ) /* DIP-SW2 */
	AM_RANGE(0x32, 0x32) AM_READWRITE( pc88sr_inport_32, pc88sr_outport_32 )
	AM_RANGE(0x34, 0x35) AM_WRITE( pc88sr_ALU )
	AM_RANGE(0x40, 0x40) AM_READWRITE( pc88sr_inport_40, pc88sr_outport_40 )
	AM_RANGE(0x44, 0x44) AM_READWRITE( YM2203_status_port_0_r, YM2203_control_port_0_w )
	AM_RANGE(0x45, 0x45) AM_READWRITE( YM2203_read_port_0_r, YM2203_write_port_0_w )
	AM_RANGE(0x46, 0x47) AM_NOP	/* OPNA extra port (not yet) */
	AM_RANGE(0x50, 0x51) AM_READWRITE( pc8801_crtc_read, pc8801_crtc_write )
	AM_RANGE(0x52, 0x5b) AM_WRITE( pc8801_palette_out )
	AM_RANGE(0x5c, 0x5c) AM_READ( pc8801_vramtest )
	AM_RANGE(0x5c, 0x5f) AM_WRITE( pc8801_vramsel )
	AM_RANGE(0x60, 0x68) AM_READWRITE( pc8801_dmac_read, pc8801_dmac_write )
	AM_RANGE(0x6e, 0x6e) AM_NOP /* CPU clock info (not yet) */
	AM_RANGE(0x6f, 0x6f) AM_NOP /* RS-232C speed ctrl (not yet) */
	AM_RANGE(0x70, 0x70) AM_READWRITE( pc8801_inport_70, pc8801_outport_70 )
	AM_RANGE(0x71, 0x71) AM_READWRITE( pc88sr_inport_71, pc88sr_outport_71 )
	AM_RANGE(0x78, 0x78) AM_WRITE( pc8801_outport_78 ) /* text window increment */
	AM_RANGE(0x90, 0x9f) AM_NOP /* CD-ROM (unknown -- not yet) */
	AM_RANGE(0xa0, 0xa3) AM_NOP /* music & network (unknown -- not yet) */
	AM_RANGE(0xa8, 0xad) AM_NOP /* second sound board (not yet) */
	AM_RANGE(0xb4, 0xb5) AM_NOP /* Video art board (unknown -- not yet) */
	AM_RANGE(0xc1, 0xc1) AM_NOP /* (unknown -- not yet) */
	AM_RANGE(0xc2, 0xcf) AM_NOP /* music (unknown -- not yet) */
	AM_RANGE(0xd0, 0xd7) AM_NOP /* music & GP-IB (unknown -- not yet) */
	AM_RANGE(0xd8, 0xd8) AM_NOP /* GP-IB (unknown -- not yet) */
	AM_RANGE(0xdc, 0xdf) AM_NOP /* MODEM (unknown -- not yet) */
	AM_RANGE(0xe2, 0xe3) AM_READWRITE( pc8801_read_extmem, pc8801_write_extmem ) /* expand RAM select */
	AM_RANGE(0xe4, 0xe4) AM_WRITE( pc8801_write_interrupt_level )
	AM_RANGE(0xe6, 0xe6) AM_WRITE( pc8801_write_interrupt_mask )
	AM_RANGE(0xe7, 0xe7) AM_NOP /* (unknown -- not yet) */
	AM_RANGE(0xe8, 0xeb) AM_READWRITE( pc8801_read_kanji1, pc8801_write_kanji1 )
	AM_RANGE(0xec, 0xed) AM_READWRITE( pc8801_read_kanji2, pc8801_write_kanji2 ) /* JIS level2 Kanji ROM */
	AM_RANGE(0xf0, 0xf1) AM_NOP /* Kana to Kanji dictionary ROM select (not yet) */
	AM_RANGE(0xf3, 0xf3) AM_NOP /* DMA floppy (unknown -- not yet) */
	AM_RANGE(0xf4, 0xf7) AM_NOP /* DMA 5'floppy (may be not released) */
	AM_RANGE(0xf8, 0xfb) AM_NOP /* DMA 8'floppy (unknown -- not yet) */
	AM_RANGE(0xfc, 0xff) AM_READWRITE( ppi8255_0_r, ppi8255_0_w )
ADDRESS_MAP_END

static INTERRUPT_GEN( pc8801fd_interrupt )
{
}

ADDRESS_MAP_START( pc8801fd_mem , ADDRESS_SPACE_PROGRAM, 8)
	AM_RANGE( 0x0000, 0x07ff) AM_ROM
	AM_RANGE( 0x4000, 0x7fff) AM_RAM
ADDRESS_MAP_END

ADDRESS_MAP_START( pc8801fd_io , ADDRESS_SPACE_IO, 8)
	ADDRESS_MAP_FLAGS( AMEF_ABITS(8) ) 
	AM_RANGE(0xf8, 0xf8) AM_READ( pc8801fd_nec765_tc )
	AM_RANGE(0xfa, 0xfa) AM_READ( nec765_status_r )
	AM_RANGE(0xfb, 0xfb) AM_READWRITE( nec765_data_r, nec765_data_w )
	AM_RANGE(0xfc, 0xff) AM_READWRITE( ppi8255_1_r, ppi8255_1_w )
ADDRESS_MAP_END

ROM_START (pc88srl)
	ROM_REGION(0x18000,REGION_CPU1,0)
	ROM_LOAD ("n80.rom", 0x00000, 0x8000, CRC(27e1857d) SHA1(5b922ed9de07d2a729bdf1da7b57c50ddf08809a))
	ROM_LOAD ("n88.rom", 0x08000, 0x8000, CRC(a0fc0473) SHA1(3b31fc68fa7f47b21c1a1cb027b86b9e87afbfff))
	ROM_LOAD ("n88_0.rom", 0x10000, 0x2000, CRC(710a63ec) SHA1(d239c26ad7ac5efac6e947b0e9549b1534aa970d))
	ROM_LOAD ("n88_1.rom", 0x12000, 0x2000, CRC(c0bd2aa6) SHA1(8528eef7946edf6501a6ccb1f416b60c64efac7c))
	ROM_LOAD ("n88_2.rom", 0x14000, 0x2000, CRC(af2b6efa) SHA1(b7c8bcea219b77d9cc3ee0efafe343cc307425d1))
	ROM_LOAD ("n88_3.rom", 0x16000, 0x2000, CRC(7713c519) SHA1(efce0b51cab9f0da6cf68507757f1245a2867a72))
	ROM_REGION(0x10000,REGION_CPU2,0)
	ROM_LOAD ("disk.rom", 0x0000, 0x0800, CRC(2158d307) SHA1(bb7103a0818850a039c67ff666a31ce49a8d516f))
	ROM_REGION(0x40000,REGION_GFX1,0)
	ROM_LOAD ("kanji1.rom", 0x00000, 0x20000, CRC(6178bd43) SHA1(82e11a177af6a5091dd67f50a2f4bafda84d6556))
	ROM_LOAD ("kanji2.rom", 0x20000, 0x20000, CRC(154803cc) SHA1(7e6591cd465cbb35d6d3446c5a83b46d30fafe95))
ROM_END

ROM_START (pc88srh)
	ROM_REGION(0x18000,REGION_CPU1,0)
	ROM_LOAD ("n80.rom", 0x00000, 0x8000, CRC(27e1857d) SHA1(5b922ed9de07d2a729bdf1da7b57c50ddf08809a))
	ROM_LOAD ("n88.rom", 0x08000, 0x8000, CRC(a0fc0473) SHA1(3b31fc68fa7f47b21c1a1cb027b86b9e87afbfff))
	ROM_LOAD ("n88_0.rom", 0x10000, 0x2000, CRC(710a63ec) SHA1(d239c26ad7ac5efac6e947b0e9549b1534aa970d))
	ROM_LOAD ("n88_1.rom", 0x12000, 0x2000, CRC(c0bd2aa6) SHA1(8528eef7946edf6501a6ccb1f416b60c64efac7c))
	ROM_LOAD ("n88_2.rom", 0x14000, 0x2000, CRC(af2b6efa) SHA1(b7c8bcea219b77d9cc3ee0efafe343cc307425d1))
	ROM_LOAD ("n88_3.rom", 0x16000, 0x2000, CRC(7713c519) SHA1(efce0b51cab9f0da6cf68507757f1245a2867a72))
	ROM_REGION(0x10000,REGION_CPU2,0)
	ROM_LOAD ("disk.rom", 0x0000, 0x0800, CRC(2158d307) SHA1(bb7103a0818850a039c67ff666a31ce49a8d516f))
	ROM_REGION(0x40000,REGION_GFX1,0)
	ROM_LOAD ("kanji1.rom", 0x00000, 0x20000, CRC(6178bd43) SHA1(82e11a177af6a5091dd67f50a2f4bafda84d6556))
	ROM_LOAD ("kanji2.rom", 0x20000, 0x20000, CRC(154803cc) SHA1(7e6591cd465cbb35d6d3446c5a83b46d30fafe95))
ROM_END

static  READ8_HANDLER(opn_dummy_input){return 0xff;}

static struct YM2203interface ym2203_interface =
{
	opn_dummy_input,
	opn_dummy_input,
	0,
	0,
	pc88sr_sound_interupt
};


static MACHINE_DRIVER_START( pc88srl )
	/* basic machine hardware */

	/* main CPU */
	MDRV_CPU_ADD_TAG("main", Z80, 4000000)        /* 4 Mhz */
	MDRV_CPU_PROGRAM_MAP(pc8801_mem, 0)
	MDRV_CPU_IO_MAP(pc88sr_io, 0)
	MDRV_CPU_VBLANK_INT(pc8801_interrupt,1)

	/* sub CPU(5 inch floppy drive) */
	MDRV_CPU_ADD_TAG("sub", Z80, 4000000)		/* 4 Mhz */
	MDRV_CPU_PROGRAM_MAP(pc8801fd_mem, 0)
	MDRV_CPU_IO_MAP(pc8801fd_io, 0)
	MDRV_CPU_VBLANK_INT(pc8801fd_interrupt,1)

	MDRV_SCREEN_REFRESH_RATE(60)
	MDRV_SCREEN_VBLANK_TIME(DEFAULT_REAL_60HZ_VBLANK_DURATION)
	MDRV_INTERLEAVE(5000)

	MDRV_MACHINE_RESET( pc88srl )

    /* video hardware */
	MDRV_VIDEO_ATTRIBUTES(VIDEO_TYPE_RASTER)
	MDRV_SCREEN_FORMAT(BITMAP_FORMAT_INDEXED16)
	/*MDRV_ASPECT_RATIO(8,5)*/
	MDRV_SCREEN_SIZE(640, 200)
	MDRV_SCREEN_VISIBLE_AREA(0, 640-1, 0, 200-1)
	MDRV_GFXDECODE( gfxdecodeinfo )
	MDRV_PALETTE_LENGTH(18)
	MDRV_COLORTABLE_LENGTH(32)
	MDRV_PALETTE_INIT( pc8801 )

	MDRV_VIDEO_START(pc8801)
	MDRV_VIDEO_UPDATE(pc8801)

	/* sound hardware */
	MDRV_SPEAKER_STANDARD_MONO("mono")
	MDRV_SOUND_ADD(YM2203, 3993600)
	MDRV_SOUND_CONFIG(ym2203_interface)	/* Should be accurate */
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "mono", 1.00)
	MDRV_SOUND_ADD(BEEP, 0)
	MDRV_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.10)
MACHINE_DRIVER_END


static MACHINE_DRIVER_START( pc88srh )
	MDRV_IMPORT_FROM( pc88srl )
	MDRV_SCREEN_REFRESH_RATE(50)
	MDRV_INTERLEAVE(6000)

	MDRV_MACHINE_RESET( pc88srh )

	MDRV_VIDEO_ATTRIBUTES(VIDEO_TYPE_RASTER)
	MDRV_SCREEN_FORMAT(BITMAP_FORMAT_INDEXED16)
	/*MDRV_ASPECT_RATIO(8, 5)*/
	MDRV_SCREEN_SIZE(640, 400)
	MDRV_SCREEN_VISIBLE_AREA(0, 640-1, 0, 400-1)
MACHINE_DRIVER_END

static void pc88_floppy_getinfo(const device_class *devclass, UINT32 state, union devinfo *info)
{
	/* floppy */
	switch(state)
	{
		/* --- the following bits of info are returned as 64-bit signed integers --- */
		case DEVINFO_INT_TYPE:							info->i = IO_FLOPPY; break;
		case DEVINFO_INT_READABLE:						info->i = 1; break;
		case DEVINFO_INT_WRITEABLE:						info->i = 1; break;
		case DEVINFO_INT_CREATABLE:						info->i = 0; break;
		case DEVINFO_INT_COUNT:							info->i = 2; break;

		/* --- the following bits of info are returned as pointers to data or functions --- */
		case DEVINFO_PTR_INIT:							info->init = device_init_d88image_floppy; break;
		case DEVINFO_PTR_LOAD:							info->load = device_load_d88image_floppy; break;
		case DEVINFO_PTR_STATUS:						/* info->status = floppy_status; */ break;

		/* --- the following bits of info are returned as NULL-terminated strings --- */
		case DEVINFO_STR_FILE_EXTENSIONS:				strcpy(info->s = device_temp_str(), "d88"); break;
	}
}

SYSTEM_CONFIG_START(pc88)
	CONFIG_DEVICE(pc88_floppy_getinfo)
SYSTEM_CONFIG_END


/*	  YEAR	NAME	  PARENT	COMPAT	MACHINE   INPUT		INIT	CONFIG	COMPANY	FULLNAME */
COMP( 1985, pc88srl, 0,		0,		pc88srl,  pc88sr,	0,		pc88,	"Nippon Electronic Company",  "PC-8801 MKIISR (Lores display, VSYNC 15KHz)", 0 )
COMP( 1985, pc88srh, pc88srl,	0,		pc88srh,  pc88sr,	0,		pc88,	"Nippon Electronic Company",  "PC-8801 MKIISR (Hires display, VSYNC 24KHz)", 0 )
