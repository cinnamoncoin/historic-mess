/***************************************************************************
Xain'd Sleena (TECHNOS), Solar Warrior (TAITO).
By Carlos A. Lozano & Rob Rosenbrock & Phil Stroffolino

	- MC68B09EP (2)
	- 6809EP (1)
	- 68705 (dump not available; patched out in the bootleg)
	- ym2203 (2)

TODO:
	- understand how the vblank bit really works
	- understand who triggers NMI and FIRQ on the first CPU
	- 68705 protection (currently patched out, causes partial missing sprites)

***************************************************************************/

#include "driver.h"
#include "vidhrdw/generic.h"
#include "cpu/m6809/m6809.h"

static unsigned char *xain_sharedram;

void xain_vh_screenrefresh(struct osd_bitmap *bitmap,int full_refresh);
int xain_vh_start(void);
void xain_scrollxP0_w(int offset,int data);
void xain_scrollyP0_w(int offset,int data);
void xain_scrollxP1_w(int offset,int data);
void xain_scrollyP1_w(int offset,int data);
void xain_charram_w(int offset,int data);
void xain_bgram0_w(int offset,int data);
void xain_bgram1_w(int offset,int data);
void xain_flipscreen_w(int offset,int data);

extern unsigned char *xain_charram, *xain_bgram0, *xain_bgram1;


int xain_sharedram_r(int offset)
{
	return xain_sharedram[offset];
}

void xain_sharedram_w(int offset, int data)
{
	/* locations 003d and 003e are used as a semaphores between CPU A and B, */
	/* so let's resync every time they are changed to avoid deadlocks */
	if ((offset == 0x003d || offset == 0x003e)
			&& xain_sharedram[offset] != data)
		cpu_yield();
	xain_sharedram[offset] = data;
}

void xainCPUA_bankswitch_w(int offset,int data)
{
	unsigned char *RAM = memory_region(REGION_CPU1);

	if (data & 0x08) {cpu_setbank(1,&RAM[0x10000]);}
	else {cpu_setbank(1,&RAM[0x4000]);}
}

void xainCPUB_bankswitch_w(int offset,int data)
{
	unsigned char *RAM = memory_region(REGION_CPU2);

	if (data & 0x01) {cpu_setbank(2,&RAM[0x10000]);}
	else {cpu_setbank(2,&RAM[0x4000]);}
}

void xain_sound_command_w(int offset, int data)
{
	soundlatch_w(offset,data);
	cpu_cause_interrupt(2,M6809_INT_IRQ);
}

static void xain_irqA_assert_w(int offset,int data)
{
	cpu_set_irq_line(0,M6809_IRQ_LINE,ASSERT_LINE);
}

static void xain_irqA_clear_w(int offset,int data)
{
	cpu_set_irq_line(0,M6809_IRQ_LINE,CLEAR_LINE);
}

static void xain_firqA_clear_w(int offset,int data)
{
	cpu_set_irq_line(0,M6809_FIRQ_LINE,CLEAR_LINE);
}

static void xain_irqB_assert_w(int offset,int data)
{
	cpu_set_irq_line(1,M6809_IRQ_LINE,ASSERT_LINE);
}

static void xain_irqB_clear_w(int offset,int data)
{
	cpu_set_irq_line(1,M6809_IRQ_LINE,CLEAR_LINE);
}

static int xain_68705_r(int offset)
{
//	if (errorlog) fprintf(errorlog,"read 68705\n");
	return 0x4d;	/* fake P5 checksum test pass */
}

static void xain_68705_w(int offset,int data)
{
//	if (errorlog) fprintf(errorlog,"write %02x to 68705\n",data);
}

static int xainA_interrupt(void)
{
	/* returning nmi on iloops() == 0 will cause lockups because the nmi handler */
	/* waits for the vblank bit to be clear and there are other places in the code */
	/* that wait for it to be set */
	if (cpu_getiloops() == 2)
		cpu_set_nmi_line(0,PULSE_LINE);
	else
		cpu_set_irq_line(0,M6809_FIRQ_LINE,ASSERT_LINE);
	return M6809_INT_NONE;
}



static struct MemoryReadAddress readmem[] =
{
	{ 0x0000, 0x1fff, xain_sharedram_r },
	{ 0x2000, 0x37ff, MRA_RAM },
	{ 0x3a00, 0x3a00, input_port_0_r },
	{ 0x3a01, 0x3a01, input_port_1_r },
	{ 0x3a02, 0x3a02, input_port_2_r },
	{ 0x3a03, 0x3a03, input_port_3_r },
	{ 0x3a04, 0x3a04, xain_68705_r },	/* from the 68705 */
	{ 0x3a05, 0x3a05, input_port_4_r },
//	{ 0x3a06, 0x3a06, MRA_NOP },	/* ?? read (and discarded) on startup. Maybe reset the 68705 */
	{ 0x4000, 0x7fff, MRA_BANK1 },
	{ 0x8000, 0xffff, MRA_ROM },
	{ -1 }	/* end of table */
};

static struct MemoryWriteAddress writemem[] =
{
	{ 0x0000, 0x1fff, xain_sharedram_w, &xain_sharedram },
	{ 0x2000, 0x27ff, xain_charram_w, &xain_charram },
	{ 0x2800, 0x2fff, xain_bgram1_w, &xain_bgram1 },
	{ 0x3000, 0x37ff, xain_bgram0_w, &xain_bgram0 },
	{ 0x3800, 0x397f, MWA_RAM, &spriteram, &spriteram_size },
	{ 0x3a00, 0x3a01, xain_scrollxP1_w },
	{ 0x3a02, 0x3a03, xain_scrollyP1_w },
	{ 0x3a04, 0x3a05, xain_scrollxP0_w },
	{ 0x3a06, 0x3a07, xain_scrollyP0_w },
	{ 0x3a08, 0x3a08, xain_sound_command_w },
	{ 0x3a09, 0x3a09, MWA_NOP },	/* NMI acknowledge */
	{ 0x3a0a, 0x3a0a, xain_firqA_clear_w },
	{ 0x3a0b, 0x3a0b, xain_irqA_clear_w },
	{ 0x3a0c, 0x3a0c, xain_irqB_assert_w },
	{ 0x3a0d, 0x3a0d, xain_flipscreen_w },
	{ 0x3a0e, 0x3a0e, xain_68705_w },	/* to 68705 */
	{ 0x3a0f, 0x3a0f, xainCPUA_bankswitch_w },
	{ 0x3c00, 0x3dff, paletteram_xxxxBBBBGGGGRRRR_split1_w, &paletteram },
	{ 0x3e00, 0x3fff, paletteram_xxxxBBBBGGGGRRRR_split2_w, &paletteram_2 },
	{ 0x4000, 0xffff, MWA_ROM },
	{ -1 }	/* end of table */
};

static struct MemoryReadAddress readmemB[] =
{
	{ 0x0000, 0x1fff, xain_sharedram_r },
	{ 0x4000, 0x7fff, MRA_BANK2 },
	{ 0x8000, 0xffff, MRA_ROM },
	{ -1 }	/* end of table */
};

static struct MemoryWriteAddress writememB[] =
{
	{ 0x0000, 0x1fff, xain_sharedram_w },
	{ 0x2000, 0x2000, xain_irqA_assert_w },
	{ 0x2800, 0x2800, xain_irqB_clear_w },
	{ 0x3000, 0x3000, xainCPUB_bankswitch_w },
	{ 0x4000, 0xffff, MWA_ROM },
	{ -1 }	/* end of table */
};

static struct MemoryReadAddress sound_readmem[] =
{
	{ 0x0000, 0x07ff, MRA_RAM },
	{ 0x1000, 0x1000, soundlatch_r },
	{ 0x8000, 0xffff, MRA_ROM },
	{ -1 }	/* end of table */
};

static struct MemoryWriteAddress sound_writemem[] =
{
	{ 0x0000, 0x07ff, MWA_RAM },
	{ 0x2800, 0x2800, YM2203_control_port_0_w },
	{ 0x2801, 0x2801, YM2203_write_port_0_w },
	{ 0x3000, 0x3000, YM2203_control_port_1_w },
	{ 0x3001, 0x3001, YM2203_write_port_1_w },
	{ 0x4000, 0xffff, MWA_ROM },
	{ -1 }	/* end of table */
};



INPUT_PORTS_START( xsleena )
	PORT_START	/* IN0 */
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_8WAY )
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT  | IPF_8WAY )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_UP    | IPF_8WAY )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN  | IPF_8WAY )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_BUTTON1 )
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_BUTTON2 )
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_START1 )
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_START2 )

	PORT_START	/* IN1 */
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_8WAY | IPF_COCKTAIL )
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT  | IPF_8WAY | IPF_COCKTAIL )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_UP    | IPF_8WAY | IPF_COCKTAIL )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN  | IPF_8WAY | IPF_COCKTAIL )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_COCKTAIL )
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_BUTTON2 | IPF_COCKTAIL )
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_COIN1 )
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_COIN2 )

	PORT_START	/* DSW0 */
	PORT_DIPNAME( 0x03, 0x03, DEF_STR( Coin_B ) )
	PORT_DIPSETTING(    0x00, DEF_STR( 2C_1C ) )
	PORT_DIPSETTING(    0x03, DEF_STR( 1C_1C ) )
	PORT_DIPSETTING(    0x02, DEF_STR( 1C_2C ) )
	PORT_DIPSETTING(    0x01, DEF_STR( 1C_3C ) )
	PORT_DIPNAME( 0x0c, 0x0c, DEF_STR( Coin_A ) )
	PORT_DIPSETTING(    0x00, DEF_STR( 2C_1C ) )
	PORT_DIPSETTING(    0x0c, DEF_STR( 1C_1C ) )
	PORT_DIPSETTING(    0x08, DEF_STR( 1C_2C ) )
	PORT_DIPSETTING(    0x04, DEF_STR( 1C_3C ) )
	PORT_DIPNAME( 0x10, 0x10, DEF_STR( Demo_Sounds ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x10, DEF_STR( On ) )
	PORT_DIPNAME( 0x20, 0x20, "Allow Continue" )
	PORT_DIPSETTING(    0x00, DEF_STR( No ) )
	PORT_DIPSETTING(    0x20, DEF_STR( Yes ) )
	PORT_DIPNAME( 0x40, 0x00, DEF_STR( Cabinet ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Upright ) )
	PORT_DIPSETTING(    0x40, DEF_STR( Cocktail ) )
	PORT_DIPNAME( 0x80, 0x00, DEF_STR( Flip_Screen ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x80, DEF_STR( On ) )

	PORT_START	/* DSW1 */
	PORT_DIPNAME( 0x03, 0x03, DEF_STR( Difficulty ) )
	PORT_DIPSETTING(    0x03, "Easy" )
	PORT_DIPSETTING(    0x02, "Normal" )
	PORT_DIPSETTING(    0x01, "Hard" )
	PORT_DIPSETTING(    0x00, "Hardest" )
	PORT_DIPNAME( 0x0c, 0x0c, "Game Time" )
	PORT_DIPSETTING(    0x0c, "Slow" )
	PORT_DIPSETTING(    0x08, "Normal" )
	PORT_DIPSETTING(    0x04, "Fast" )
	PORT_DIPSETTING(    0x00, "Very Fast" )
	PORT_DIPNAME( 0x30, 0x30, DEF_STR( Bonus_Life ) )
	PORT_DIPSETTING(    0x30, "20k 70k and every 70k" )
	PORT_DIPSETTING(    0x20, "30k 80k and every 80k" )
	PORT_DIPSETTING(    0x10, "20k and 80k" )
	PORT_DIPSETTING(    0x00, "30k and 80k" )
	PORT_DIPNAME( 0xc0, 0xc0, DEF_STR( Lives ) )
	PORT_DIPSETTING(    0xc0, "3")
	PORT_DIPSETTING(    0x80, "4")
	PORT_DIPSETTING(    0x40, "6")
	PORT_BITX( 0,       0x00, IPT_DIPSWITCH_SETTING | IPF_CHEAT, "Infinite", IP_KEY_NONE, IP_JOY_NONE )

	PORT_START	/* IN2 */
	PORT_BIT( 0x03, IP_ACTIVE_LOW,  IPT_UNUSED )
	PORT_BIT( 0x04, IP_ACTIVE_LOW,  IPT_COIN3 )
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_UNKNOWN )	/* when 0, 68705 is ready to send data */
	PORT_BIT( 0x10, IP_ACTIVE_LOW,  IPT_UNKNOWN )	/* when 1, 68705 is ready to receive data */
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_VBLANK )
	PORT_BIT( 0xc0, IP_ACTIVE_LOW,  IPT_UNUSED )
INPUT_PORTS_END



static struct GfxLayout charlayout =
{
	8,8,	/* 8*8 chars */
	1024,	/* 1024 characters */
	4,	/* 4 bits per pixel */
	{ 0, 2, 4, 6 },	/* plane offset */
	{ 1, 0, 8*8+1, 8*8+0, 16*8+1, 16*8+0, 24*8+1, 24*8+0 },
	{ 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8 },
	32*8	/* every char takes 32 consecutive bytes */
};

static struct GfxLayout tilelayout =
{
	16,16,	/* 8*8 chars */
	4*512,	/* 512 characters */
	4,	/* 4 bits per pixel */
	{ 0x8000*4*8+0, 0x8000*4*8+4, 0, 4 },	/* plane offset */
	{ 3, 2, 1, 0, 16*8+3, 16*8+2, 16*8+1, 16*8+0,
	  32*8+3,32*8+2 ,32*8+1 ,32*8+0 ,48*8+3 ,48*8+2 ,48*8+1 ,48*8+0 },
	{ 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8,
	  8*8, 9*8, 10*8, 11*8, 12*8, 13*8, 14*8, 15*8 },
	64*8	/* every char takes 64 consecutive bytes */
};

static struct GfxDecodeInfo gfxdecodeinfo[] =
{
	{ 1, 0x00000, &charlayout,   0, 8 },	/* 8x8 text */
	{ 1, 0x08000, &tilelayout, 256, 8 },	/* 16x16 Background */
	{ 1, 0x48000, &tilelayout, 384, 8 },	/* 16x16 Background */
	{ 1, 0x88000, &tilelayout, 128, 8 },	/* Sprites */
	{ -1 } /* end of array */
};



/* handler called by the 2203 emulator when the internal timers cause an IRQ */
static void irqhandler(int irq)
{
	cpu_set_irq_line(2,M6809_FIRQ_LINE,irq ? ASSERT_LINE : CLEAR_LINE);
}

static struct YM2203interface ym2203_interface =
{
	2,			/* 2 chips */
	3000000,	/* 3 MHz ??? */
	{ YM2203_VOL(40,50), YM2203_VOL(40,50) },
	{ 0 },
	{ 0 },
	{ 0 },
	{ 0 },
	{ irqhandler }
};



static struct MachineDriver machine_driver =
{
	{
		{
			CPU_M6809,
			2000000,	/* 2 Mhz ??? */
			readmem,writemem,0,0,
			xainA_interrupt,4	/* wrong, this is just a hack */
								/* IRQs are caused by CPU B */
								/* FIRQs are caused by ? */
								/* NMIs are caused by... vblank it seems, but it checks */
								/* the vblank bit before RTI, and there are other places in */
								/* the code that check that bit, so it would cause lockups */
		},
		{
			CPU_M6809,
			2000000,	/* 2 Mhz ??? */
			readmemB,writememB,0,0,
			ignore_interrupt,0	/* IRQs are caused by CPU A */
		},
		{
			CPU_M6809 | CPU_AUDIO_CPU,
			2000000,	/* 2 Mhz ??? */
			sound_readmem,sound_writemem,0,0,
			ignore_interrupt,0	/* IRQs are caused by CPU A */
								/* FIRQs are caused by the YM2203 */
		},
	},
	60, DEFAULT_REAL_60HZ_VBLANK_DURATION,
	100,	/* 100 CPU slice per frame */
	0,

	/* video hardware */
	32*8, 32*8, { 0*8, 32*8-1, 2*8, 30*8-1 },
	gfxdecodeinfo,
	512, 512,
	0,

	VIDEO_TYPE_RASTER | VIDEO_MODIFIES_PALETTE,
	0,
	xain_vh_start,
	0,
	xain_vh_screenrefresh,

	/* sound hardware */
	0,0,0,0,
	{
		{
			SOUND_YM2203,
			&ym2203_interface
		}
	}
};



/***************************************************************************

  Game driver(s)

***************************************************************************/

ROM_START( xsleena )
	ROM_REGIONX( 0x14000, REGION_CPU1 )	/* 64k for code */
	ROM_LOAD( "s-10.7d",      0x08000, 0x8000, 0x370164be )
	ROM_LOAD( "s-11.7c",      0x04000, 0x4000, 0xd22bf859 )
	ROM_CONTINUE(             0x10000, 0x4000 )

	ROM_REGION_DISPOSE(0xc8000)     /* temporary space for graphics (disposed after conversion) */
	ROM_LOAD( "s-12.8b",      0x00000, 0x8000, 0x83c00dd8 ) /* chars */
	ROM_LOAD( "s-21.16i",     0x08000, 0x8000, 0x11eb4247 ) /* tiles */
	ROM_LOAD( "s-22.15i",     0x10000, 0x8000, 0x422b536e )
	ROM_LOAD( "s-23.14i",     0x18000, 0x8000, 0x828c1b0c )
	ROM_LOAD( "s-24.13i",     0x20000, 0x8000, 0xd37939e0 )
	ROM_LOAD( "s-13.16g",     0x28000, 0x8000, 0x8f0aa1a7 )
	ROM_LOAD( "s-14.15g",     0x30000, 0x8000, 0x45681910 )
	ROM_LOAD( "s-15.14g",     0x38000, 0x8000, 0xa8eeabc8 )
	ROM_LOAD( "s-16.13g",     0x40000, 0x8000, 0xe59a2f27 )
	ROM_LOAD( "s-6.4h",       0x48000, 0x8000, 0x5c6c453c ) /* tiles */
	ROM_LOAD( "s-5.4l",       0x50000, 0x8000, 0x59d87a9a )
	ROM_LOAD( "s-4.4m",       0x58000, 0x8000, 0x84884a2e )
	/* 0x60000-0x67fff empty */
	ROM_LOAD( "s-7.4f",       0x68000, 0x8000, 0x8d637639 )
	ROM_LOAD( "s-8.4d",       0x70000, 0x8000, 0x71eec4e6 )
	ROM_LOAD( "s-9.4c",       0x78000, 0x8000, 0x7fc9704f )
	/* 0x80000-0x87fff empty */
	ROM_LOAD( "s-25.10i",     0x88000, 0x8000, 0x252976ae ) /* sprites */
	ROM_LOAD( "s-26.9i",      0x90000, 0x8000, 0xe6f1e8d5 )
	ROM_LOAD( "s-27.8i",      0x98000, 0x8000, 0x785381ed )
	ROM_LOAD( "s-28.7i",      0xa0000, 0x8000, 0x59754e3d )
	ROM_LOAD( "s-17.10g",     0xa8000, 0x8000, 0x4d977f33 )
	ROM_LOAD( "s-18.9g",      0xb0000, 0x8000, 0x3f3b62a0 )
	ROM_LOAD( "s-19.8g",      0xb8000, 0x8000, 0x76641ee3 )
	ROM_LOAD( "s-20.7g",      0xc0000, 0x8000, 0x37671f36 )

	ROM_REGIONX( 0x14000, REGION_CPU2 )	/* 64k for code */
	ROM_LOAD( "s-2.3b",       0x08000, 0x8000, 0xa1a860e2 )
	ROM_LOAD( "s-1.2b",       0x04000, 0x4000, 0x948b9757 )
	ROM_CONTINUE(       0x10000, 0x4000 )

	ROM_REGIONX( 0x10000, REGION_CPU3 )	/* 64k for code */
	ROM_LOAD( "s-3.4s",       0x8000, 0x8000, 0xa5318cb8 )

	ROM_REGION(0x0100)	/* PROM */
	ROM_LOAD( "mb7114e.59",   0x0000, 0x0100, 0xfed32888 )	/* timing? (not used) */
ROM_END

ROM_START( xsleenab )
	ROM_REGIONX( 0x14000, REGION_CPU1 )	/* 64k for code */
	ROM_LOAD( "1.rom",        0x08000, 0x8000, 0x79f515a7 )
	ROM_LOAD( "s-11.7c",      0x04000, 0x4000, 0xd22bf859 )
	ROM_CONTINUE(             0x10000, 0x4000 )

	ROM_REGION_DISPOSE(0xc8000)     /* temporary space for graphics (disposed after conversion) */
	ROM_LOAD( "s-12.8b",      0x00000, 0x8000, 0x83c00dd8 ) /* chars */
	ROM_LOAD( "s-21.16i",     0x08000, 0x8000, 0x11eb4247 ) /* tiles */
	ROM_LOAD( "s-22.15i",     0x10000, 0x8000, 0x422b536e )
	ROM_LOAD( "s-23.14i",     0x18000, 0x8000, 0x828c1b0c )
	ROM_LOAD( "s-24.13i",     0x20000, 0x8000, 0xd37939e0 )
	ROM_LOAD( "s-13.16g",     0x28000, 0x8000, 0x8f0aa1a7 )
	ROM_LOAD( "s-14.15g",     0x30000, 0x8000, 0x45681910 )
	ROM_LOAD( "s-15.14g",     0x38000, 0x8000, 0xa8eeabc8 )
	ROM_LOAD( "s-16.13g",     0x40000, 0x8000, 0xe59a2f27 )
	ROM_LOAD( "s-6.4h",       0x48000, 0x8000, 0x5c6c453c ) /* tiles */
	ROM_LOAD( "s-5.4l",       0x50000, 0x8000, 0x59d87a9a )
	ROM_LOAD( "s-4.4m",       0x58000, 0x8000, 0x84884a2e )
	/* 0x60000-0x67fff empty */
	ROM_LOAD( "s-7.4f",       0x68000, 0x8000, 0x8d637639 )
	ROM_LOAD( "s-8.4d",       0x70000, 0x8000, 0x71eec4e6 )
	ROM_LOAD( "s-9.4c",       0x78000, 0x8000, 0x7fc9704f )
	/* 0x80000-0x87fff empty */
	ROM_LOAD( "s-25.10i",     0x88000, 0x8000, 0x252976ae ) /* sprites */
	ROM_LOAD( "s-26.9i",      0x90000, 0x8000, 0xe6f1e8d5 )
	ROM_LOAD( "s-27.8i",      0x98000, 0x8000, 0x785381ed )
	ROM_LOAD( "s-28.7i",      0xa0000, 0x8000, 0x59754e3d )
	ROM_LOAD( "s-17.10g",     0xa8000, 0x8000, 0x4d977f33 )
	ROM_LOAD( "s-18.9g",      0xb0000, 0x8000, 0x3f3b62a0 )
	ROM_LOAD( "s-19.8g",      0xb8000, 0x8000, 0x76641ee3 )
	ROM_LOAD( "s-20.7g",      0xc0000, 0x8000, 0x37671f36 )

	ROM_REGIONX( 0x14000, REGION_CPU2 )	/* 64k for code */
	ROM_LOAD( "s-2.3b",       0x08000, 0x8000, 0xa1a860e2 )
	ROM_LOAD( "s-1.2b",       0x04000, 0x4000, 0x948b9757 )
	ROM_CONTINUE(       0x10000, 0x4000 )

	ROM_REGIONX( 0x10000, REGION_CPU3 )	/* 64k for code */
	ROM_LOAD( "s-3.4s",       0x8000, 0x8000, 0xa5318cb8 )

	ROM_REGION(0x0100)	/* PROM */
	ROM_LOAD( "mb7114e.59",   0x0000, 0x0100, 0xfed32888 )	/* timing? (not used) */
ROM_END

ROM_START( solarwar )
	ROM_REGIONX( 0x14000, REGION_CPU1 )	/* 64k for code */
	ROM_LOAD( "p9-0.bin",     0x08000, 0x8000, 0x8ff372a8 )
	ROM_LOAD( "pa-0.bin",     0x04000, 0x4000, 0x154f946f )
	ROM_CONTINUE(             0x10000, 0x4000 )

	ROM_REGION_DISPOSE(0xc8000)     /* temporary space for graphics (disposed after conversion) */
	ROM_LOAD( "s-12.8b",      0x00000, 0x8000, 0x83c00dd8 ) /* chars */
	ROM_LOAD( "s-21.16i",     0x08000, 0x8000, 0x11eb4247 ) /* tiles */
	ROM_LOAD( "s-22.15i",     0x10000, 0x8000, 0x422b536e )
	ROM_LOAD( "s-23.14i",     0x18000, 0x8000, 0x828c1b0c )
	ROM_LOAD( "pn-0.bin",     0x20000, 0x8000, 0xd2ed6f94 )
	ROM_LOAD( "s-13.16g",     0x28000, 0x8000, 0x8f0aa1a7 )
	ROM_LOAD( "s-14.15g",     0x30000, 0x8000, 0x45681910 )
	ROM_LOAD( "s-15.14g",     0x38000, 0x8000, 0xa8eeabc8 )
	ROM_LOAD( "pf-0.bin",     0x40000, 0x8000, 0x6e627a77 )
	ROM_LOAD( "s-6.4h",       0x48000, 0x8000, 0x5c6c453c ) /* tiles */
	ROM_LOAD( "s-5.4l",       0x50000, 0x8000, 0x59d87a9a )
	ROM_LOAD( "s-4.4m",       0x58000, 0x8000, 0x84884a2e )
	/* 0x60000-0x67fff empty */
	ROM_LOAD( "s-7.4f",       0x68000, 0x8000, 0x8d637639 )
	ROM_LOAD( "s-8.4d",       0x70000, 0x8000, 0x71eec4e6 )
	ROM_LOAD( "s-9.4c",       0x78000, 0x8000, 0x7fc9704f )
	/* 0x80000-0x87fff empty */
	ROM_LOAD( "s-25.10i",     0x88000, 0x8000, 0x252976ae )	/* sprites */
	ROM_LOAD( "s-26.9i",      0x90000, 0x8000, 0xe6f1e8d5 )
	ROM_LOAD( "s-27.8i",      0x98000, 0x8000, 0x785381ed )
	ROM_LOAD( "s-28.7i",      0xa0000, 0x8000, 0x59754e3d )
	ROM_LOAD( "s-17.10g",     0xa8000, 0x8000, 0x4d977f33 )
	ROM_LOAD( "s-18.9g",      0xb0000, 0x8000, 0x3f3b62a0 )
	ROM_LOAD( "s-19.8g",      0xb8000, 0x8000, 0x76641ee3 )
	ROM_LOAD( "s-20.7g",      0xc0000, 0x8000, 0x37671f36 )

	ROM_REGIONX( 0x14000, REGION_CPU2 )	/* 64k for code */
	ROM_LOAD( "p1-0.bin",     0x08000, 0x8000, 0xf5f235a3 )
	ROM_LOAD( "p0-0.bin",     0x04000, 0x4000, 0x51ae95ae )
	ROM_CONTINUE(         0x10000, 0x4000 )

	ROM_REGIONX( 0x10000, REGION_CPU3 )	/* 64k for code */
	ROM_LOAD( "s-3.4s",       0x8000, 0x8000, 0xa5318cb8 )

	ROM_REGION(0x0100)	/* PROM */
	ROM_LOAD( "mb7114e.59",   0x0000, 0x0100, 0xfed32888 )	/* timing? (not used) */
ROM_END



void xsleena_patch(void)
{
	unsigned char *RAM = memory_region(REGION_CPU1);

	/* do the same patch as the bootleg xsleena */
	RAM[0xd488] = 0x12;
	RAM[0xd489] = 0x12;
	RAM[0xd48a] = 0x12;
	RAM[0xd48b] = 0x12;
	RAM[0xd48c] = 0x12;
	RAM[0xd48d] = 0x12;
}

void solarwar_patch(void)
{
	unsigned char *RAM = memory_region(REGION_CPU1);

	/* do the same patch as the bootleg xsleena */
	RAM[0xd47e] = 0x12;
	RAM[0xd47f] = 0x12;
	RAM[0xd480] = 0x12;
	RAM[0xd481] = 0x12;
	RAM[0xd482] = 0x12;
	RAM[0xd483] = 0x12;
}



struct GameDriver driver_xsleena =
{
	__FILE__,
	0,
	"xsleena",
	"Xain'd Sleena",
	"1986",
	"Technos",
	"Carlos A. Lozano\nRob Rosenbrock\nPhil Stroffolino\n",
	0,
	&machine_driver,
	xsleena_patch,

	rom_xsleena,
	0, 0,
	0,
	0,

	input_ports_xsleena,

	0, 0, 0,
	ROT0,
	0,0
};

struct GameDriver driver_xsleenab =
{
	__FILE__,
	&driver_xsleena,
	"xsleenab",
	"Xain'd Sleena (bootleg)",
	"1986",
	"bootleg",
	"Carlos A. Lozano\nRob Rosenbrock\nPhil Stroffolino\n",
	0,
	&machine_driver,
	0,

	rom_xsleenab,
	0, 0,
	0,
	0,

	input_ports_xsleena,

	0, 0, 0,
	ROT0,
	0,0
};

struct GameDriver driver_solarwar =
{
	__FILE__,
	&driver_xsleena,
	"solarwar",
	"Solar Warrior",
	"1986",
	"[Technos] Taito (Memetron license)",
	"Carlos A. Lozano\nRob Rosenbrock\nPhil Stroffolino\n",
	0,
	&machine_driver,
	solarwar_patch,

	rom_solarwar,
	0, 0,
	0,
	0,

	input_ports_xsleena,

	0, 0, 0,
	ROT0,
	0,0
};
