/***************************************************************************

  machine.c

  Functions to emulate general aspects of the machine (RAM, ROM, interrupts,
  I/O ports)

***************************************************************************/

#include "driver.h"
#include "cpu/m6809/m6809.h"
#include "machine/6821pia.h"

UINT8 *coco_rom;
UINT8 *coco_ram;

/* from vidhrdw/dragon.c */
extern void coco3_ram_b1_w (int offset, int data);
extern void coco3_ram_b2_w (int offset, int data);
extern void coco3_ram_b3_w (int offset, int data);
extern void coco3_ram_b4_w (int offset, int data);
extern void coco3_ram_b5_w (int offset, int data);
extern void coco3_ram_b6_w (int offset, int data);
extern void coco3_ram_b7_w (int offset, int data);
extern void coco3_ram_b8_w (int offset, int data);
extern void coco3_vh_sethires(int hires);

extern void d_pia1_pb_w(int offset, int data);

static void d_pia1_pa_w(int offset, int data);
static int  d_pia1_cb1_r(int offset);
static int  d_pia0_ca1_r(int offset);
static int  d_pia0_pa_r(int offset);
static int  d_pia1_pa_r(int offset);
static void d_pia0_pb_w(int offset, int data);
static void d_pia1_cb2_w(int offset, int data);
static void d_pia0_cb2_w(int offset, int data);
static void d_pia1_ca2_w(int offset, int data);
static void d_pia0_ca2_w(int offset, int data);
static void d_pia0_irq_b(int state);

static void coco_pia1_ca2_w(int offset, int data);

static struct pia6821_interface dragon_pia_0_intf =
{
	/*inputs : A/B,CA/B1,CA/B2 */ d_pia0_pa_r, 0, d_pia0_ca1_r, 0, 0, 0,
	/*outputs: A/B,CA/B2	   */ 0, d_pia0_pb_w, d_pia0_ca2_w, d_pia0_cb2_w,
	/*irqs	 : A/B			   */ 0, d_pia0_irq_b
};

static struct pia6821_interface dragon_pia_1_intf =
{
	/*inputs : A/B,CA/B1,CA/B2 */ d_pia1_pa_r, 0, 0, d_pia1_cb1_r, 0, 0,
	/*outputs: A/B,CA/B2	   */ d_pia1_pa_w, d_pia1_pb_w, d_pia1_ca2_w, d_pia1_cb2_w,
	/*irqs	 : A/B			   */ 0, 0
};

static struct pia6821_interface coco_pia_1_intf =
{
	/*inputs : A/B,CA/B1,CA/B2 */ d_pia1_pa_r, 0, 0, d_pia1_cb1_r, 0, 0,
	/*outputs: A/B,CA/B2	   */ d_pia1_pa_w, d_pia1_pb_w, coco_pia1_ca2_w, d_pia1_cb2_w,
	/*irqs	 : A/B			   */ 0, 0
};

/* initialized in dragon_floppy_init */
static const char *floppy_name[4] = { NULL, NULL, NULL, NULL };

static UINT8 *tape_start, *tape_position;
static int cart_inserted;
static int tape_size;
static UINT8 pia0_pb, sound_mux, tape_motor;
static UINT8 joystick_axis, joystick;
static int d_dac;

/***************************************************************************
  dev init
***************************************************************************/

static int generic_rom_load(UINT8 *rambase, UINT8 *rombase, const char *name)
{
	void *fp;

	typedef struct {
		UINT8 length_lsb;
		UINT8 length_msb;
		UINT8 start_lsb;
		UINT8 start_msb;
	} pak_header;

	if (strlen(name)!=0)
	{
		if (!(fp = osd_fopen (Machine->gamedrv->name, name, OSD_FILETYPE_IMAGE_R, 0)))
		{
			if (errorlog) fprintf(errorlog,"Unable to locate ROM: %s\n",name);
			return 1;
		}
		else
		{
			/* PAK file */

			/* PAK files have the following format:
			 *
			 * length		(two bytes, little endian)
			 * base address (two bytes, little endian, typically 0xc000)
			 * ...data...
			 */
			int paklength;
			int pakstart;
			pak_header header;

			if (osd_fread(fp, &header, sizeof(header)) < sizeof(header))
			{
				if (errorlog) fprintf(errorlog,"Could not fully read PAK.\n");
				return 1;
			}

			paklength = (((int) header.length_msb) << 8) | (int) header.length_lsb;
			pakstart = (((int) header.start_msb) << 8) | (int) header.start_lsb;

			/* Since PAK files allow the flexibility of loading anywhere in
			 * the base RAM or ROM, we have to do tricks because in MESS's
			 * memory, RAM and ROM may be separated, hense this function's
			 * two parameters.
			 */

				/* Get the RAM portion */
			if (pakstart < 0x8000) {
				int ram_paklength;

				ram_paklength = (paklength > (0x8000 - pakstart)) ? (0x8000 - pakstart) : paklength;
				if (ram_paklength) {
					if (osd_fread(fp, &rambase[pakstart], ram_paklength) < ram_paklength)
					{
						if (errorlog) fprintf(errorlog,"Could not fully read PAK.\n");
						osd_fclose(fp);
						return 1;
					}
					pakstart += ram_paklength;
					paklength -= ram_paklength;
				}
			}

			/* Get the ROM portion */
			if (paklength) {
				if (osd_fread(fp, &rombase[pakstart - 0x8000], paklength) < paklength)
				{
					if (errorlog) fprintf(errorlog,"Could not fully read PAK.\n");
					return 1;
				}
				cart_inserted = 1;
			}
			osd_fclose(fp);
			/* One thing I _don_t_ do yet is set the program counter properly... */
		}
	}
	return 0;
}

int coco_cassette_init(int id, const char *name)
{
	void *fp;

    tape_start = NULL;
    if (strlen(name)!=0)
	{
		/* Tape */
		if (!(fp = osd_fopen (Machine->gamedrv->name, name, OSD_FILETYPE_IMAGE_R, 0)))
		{
			if (errorlog) fprintf(errorlog,"Unable to locate cassette: %s\n",name);
			return 1;
		}
		else
		{
			tape_size = osd_fsize(fp);
			if ((tape_start = (UINT8 *)malloc(tape_size)) == NULL)
			{
				if (errorlog) fprintf(errorlog,"Not enough memory.\n");
				return 1;
			}
			osd_fread(fp, tape_start, tape_size);
			osd_fclose(fp);
		}
	}
	tape_position = tape_start;
	return 0;
}

int dragon32_rom_load(int id, const char *name)
{
	UINT8 *ROM = memory_region(REGION_CPU1);
	return generic_rom_load(&ROM[0], &ROM[0x8000], name);
}

int dragon64_rom_load(int id, const char *name)
{
	UINT8 *ROM = memory_region(REGION_CPU1);
	return generic_rom_load(&ROM[0], &ROM[0x10000], name);
}

int coco3_rom_load(int id, const char *name)
{
	UINT8 *ROM = memory_region(REGION_CPU1);
	return generic_rom_load(&ROM[0x70000], &ROM[0x80000], name);
}

int dragon_mapped_irq_r(int offset)
{
	return coco_rom[0x3ff0 + offset];
}

void dragon_speedctrl_w(int offset, int data)
{
	/* The infamous speed up poke. However, I can't seem to implement
	 * it. I would be doing the following if it were legal */
/*	Machine->drv->cpu[0].cpu_clock = ((offset & 1) + 1) * 894886; */
	/* HJB: just do the following */
    timer_set_overclock(0, 1+(offset&1));
}

/***************************************************************************
  MMU
***************************************************************************/

void dragon64_enable_64k_w(int offset, int data)
{
	UINT8 *RAM = memory_region(REGION_CPU1);
	if (offset) {
		cpu_setbank(1, &RAM[0x8000]);
		cpu_setbankhandler_w(1, MWA_BANK1);
	}
	else {
		cpu_setbank(1, coco_rom);
		cpu_setbankhandler_w(1, MWA_ROM);
	}
}

/* Coco 3 */

static int coco3_enable_64k;
static int coco3_mmu[16];
static int coco3_gimereg[8];

int coco3_mmu_lookup(int block)
{
	int result;

	if (coco3_gimereg[0] & 0x40) {
		if (coco3_gimereg[1] & 1)
			block += 8;
		result = coco3_mmu[block];
	}
	else {
		result = block + 56;
	}
	return result;
}

int coco3_mmu_translate(int block, int offset)
{
	if ((block == 7) && (coco3_gimereg[0] & 8))
		return 0x7e000 + offset;
	else
		return (coco3_mmu_lookup(block) * 0x2000) + offset;
}

static void coco3_mmu_update(int lowblock, int hiblock)
{
	UINT8 *RAM = memory_region(REGION_CPU1);
	typedef void (*writehandler)(int wh_offset, int data);
	static writehandler handlers[] = {
		coco3_ram_b1_w, coco3_ram_b2_w,
		coco3_ram_b3_w, coco3_ram_b4_w,
		coco3_ram_b5_w, coco3_ram_b6_w,
		coco3_ram_b7_w, coco3_ram_b8_w
	};

	int hirom_base, lorom_base;
	int i;

	hirom_base = ((coco3_gimereg[0] & 3) == 2) ? 0x0000 : 0x8000;
	lorom_base = ((coco3_gimereg[0] & 3) != 3) ? 0x0000 : 0x8000;

	for (i = lowblock; i <= hiblock; i++) {
		if ((i >= 4) && !coco3_enable_64k) {
			cpu_setbank(i + 1, &coco_rom[(i >= 6 ? hirom_base : lorom_base) + ((i-4) * 0x2000)]);
			cpu_setbankhandler_w(i + 1, MWA_ROM);
		}
		else {
			cpu_setbank(i + 1, &RAM[coco3_mmu_lookup(i) * 0x2000]);
			cpu_setbankhandler_w(i + 1, handlers[i]);
		}
	}
}

int coco3_mmu_r(int offset)
{
	return coco3_mmu[offset];
}

void coco3_mmu_w(int offset, int data)
{
	data &= 0x3f;
	coco3_mmu[offset] = data;

	/* Did we modify the live MMU bank? */
	if ((offset >> 3) == (coco3_gimereg[1] & 1))
		coco3_mmu_update(offset & 7, offset & 7);
}

int coco3_gime_r(int offset)
{
	return coco3_gimereg[offset];
}

void coco3_gime_w(int offset, int data)
{
	coco3_gimereg[offset] = data;

	/* Features marked with '!' are not yet implemented */
	switch(offset) {
	case 0:
		/*	$FF90 Initialization register 0
		 *		? Bit 7 COCO 1=CoCo compatible mode
		 *		  Bit 6 MMUEN 1=MMU enabled
		 *		! Bit 5 IEN 1 = GIME chip IRQ enabled
		 *		! Bit 4 FEN 1 = GIME chip FIRQ enabled
		 *		  Bit 3 MC3 1 = RAM at FEXX is constant
		 *		! Bit 2 MC2 1 = standard SCS (Spare Chip Select)
		 *		  Bit 1 MC1 ROM map control
		 *		  Bit 0 MC0 ROM map control
		 */
		coco3_vh_sethires(data & 0x80 ? 0 : 1);
		coco3_mmu_update(0, 7);
		break;

	case 1:
		/*	$FF91 Initialization register 1Bit 7 Unused
		 *		  Bit 6 Unused
		 *		! Bit 5 TINS Timer input select; 1 = 70 nsec, 0 = 63.5 usec
		 *		  Bit 4 Unused
		 *		  Bit 3 Unused
		 *		  Bit 2 Unused
		 *		  Bit 1 Unused
		 *		  Bit 0 TR Task register select
		 */
		coco3_mmu_update(0, 7);
		break;

	case 2:
		/*	$FF92 Interrupt request enable register
		 *		  Bit 7 Unused
		 *		  Bit 6 Unused
		 *		! Bit 5 TMR Timer interrupt
		 *		! Bit 4 HBORD Horizontal border interrupt
		 *		! Bit 3 VBORD Vertical border interrupt
		 *		! Bit 2 EI2 Serial data interrupt
		 *		! Bit 1 EI1 Keyboard interrupt
		 *		! Bit 0 EI0 Cartridge interrupt
		 */
		break;

	case 3:
		/*	$FF93 Fast interrupt request enable register
		 *		  Bit 7 Unused
		 *		  Bit 6 Unused
		 *		! Bit 5 TMR Timer interrupt
		 *		! Bit 4 HBORD Horizontal border interrupt
		 *		! Bit 3 VBORD Vertical border interrupt
		 *		! Bit 2 EI2 Serial border interrupt
		 *		! Bit 1 EI1 Keyboard interrupt
		 *		  Bit 0 EI0 Cartridge interrupt
		 */
		break;

	case 4:
		/*	$FF94 Timer register MSB
		 *		  Bits 4-7 Unused
		 *		! Bits 0-3 High order four bits of the timer
		 */
		break;

	case 5:
		/*	$FF95 Timer register LSB
		 *		! Bits 0-7 Low order eight bits of the timer
		 */
		break;
	}
}

void coco3_enable_64k_w(int offset, int data)
{
	coco3_enable_64k = offset;
	coco3_mmu_update(4, 7);
}

/***************************************************************************
  Disk Controller
***************************************************************************/

/* NPW 1-18-2000 - This code needs to be overhauled sometime */

typedef struct {
	int motor_on;
	int track;
	int sector;
} dragon_drive;

static void *dragon_disk_fd;
static int dragon_disk_counter;
static int dragon_disk_haltenable;
static int dragon_disk_reading;
static int dragon_disk_writing;
static dragon_drive dragon_drives[4];
static int dragon_disk_status;
static int dragon_disk_datareg;

int coco_floppy_init(int id, const char *name)
{
	floppy_name[id] = name;
	dragon_disk_fd = 0;
	dragon_disk_counter = 0;
	dragon_disk_haltenable = 0;
	dragon_disk_reading = 0;
	dragon_disk_writing = 0;
	dragon_disk_status = 0;
	dragon_disk_datareg = 0;
	memset(&dragon_drives, 0, sizeof(dragon_drives));
	return 0;
}

static void close_sector(void)
{
	if (dragon_disk_fd) {
		osd_fclose(dragon_disk_fd);
		dragon_disk_fd = NULL;
	}
	dragon_disk_reading = 0;
	dragon_disk_writing = 0;
}

void coco_floppy_exit(int id)
{
	close_sector();
}

static void check_counter(void)
{
	if (dragon_disk_counter == 0) {
		if (dragon_disk_haltenable) {
			m6809_set_nmi_line(1);
			m6809_set_nmi_line(0);
		}
		close_sector();
	}
	else {
		dragon_disk_counter--;
	}
}

static int open_sector(int drive, int track, int sector)
{
	close_sector();

	/* Do we have a floppy open? */
	if (!floppy_name[drive])
		return 1;

	dragon_disk_fd = osd_fopen(Machine->gamedrv->name,floppy_name[drive],OSD_FILETYPE_IMAGE_RW,0);
	if (!dragon_disk_fd)
	{
		if (errorlog) fprintf(errorlog,"Couldn't open image.\n");
		return 1;
	}

	if (osd_fseek(dragon_disk_fd,track * 0x1200 + ((sector-1) * 0x100),SEEK_CUR)!=0)
	{
		if (errorlog) fprintf(errorlog,"Couldn't find track/sector %d/%d.\n", track, sector);
		osd_fclose(dragon_disk_fd);
		dragon_disk_fd = NULL;
		return 1;
	}

	dragon_disk_counter = 256;
	return 0;
}

static int sector_read(int *data)
{
	UINT8 d;

	if (!dragon_disk_fd || (osd_fread(dragon_disk_fd,&d,1) <1))
	{
		if (errorlog) fprintf(errorlog,"Couldn't read data from disk\n");
		return 1;
	}

	*data = d;
	check_counter();
	return 0;
}

static int sector_write(int data)
{
	UINT8 d;

	d = data;
	if (!dragon_disk_fd || (osd_fwrite(dragon_disk_fd,&d,1) <1))
	{
		if (errorlog) fprintf(errorlog,"Couldn't read data from disk\n");
		return 1;
	}

	check_counter();
	return 0;
}

/***************************************************************************/

static dragon_drive *find_drive(void)
{
	int i;
	for (i = 0; i < (sizeof(dragon_drives) / sizeof(dragon_drives[0])); i++) {
		if (dragon_drives[i].motor_on)
			return &dragon_drives[i];
	}
	return NULL;
}

/***************************************************************************/

static void set_track(int track)
{
	dragon_drive *d;

	d = find_drive();
	if (d) {
		d->track = track;
		dragon_disk_status = track ? 0 : (1 << 2);
	}
	else {
		dragon_disk_status = 0x80;
	}
}

int dragon_disk_r(int offset)
{
	dragon_drive *d;

	switch(offset) {
	case 8:
		/* status register */
		return dragon_disk_status;
	case 9:
		/* track register */
		d = find_drive();
		return d ? d->track : 0;
	case 10:
		/* sector register */
		d = find_drive();
		return d ? d->sector : 0;
	case 11:
		/* data register */
		if (dragon_disk_reading) {
			int data;
			dragon_disk_status = sector_read(&data) ? 0x80 : (dragon_disk_counter ? 0x02 : 0x00);
			return data;
		}
		break;
	default:
		if (errorlog) fprintf(errorlog,"Bad disk register read: offset=%d\n",offset);
	}
	return 0;
}

void dragon_disk_w(int offset, int data)
{
	dragon_drive *d;

	switch(offset) {
	case 0:
		/* DSKREG - the control register
		 *
		 * Bit
		 *	7 halt enable flag
		 *	6 drive select #3
		 *	5 density flag (0=single, 1=double)
		 *	4 write precompensation
		 *	3 drive motor activation
		 *	2 drive select #2
		 *	1 drive select #1
		 *	0 drive select #0
		 */
		d = find_drive();

		if (data & 0x01) {
			dragon_drives[0].motor_on = (data & 0x08) ? 1 : 0;
		}
		if (data & 0x02) {
			dragon_drives[1].motor_on = (data & 0x08) ? 1 : 0;
		}
		if (data & 0x04) {
			dragon_drives[2].motor_on = (data & 0x08) ? 1 : 0;
		}
		if (data & 0x40) {
			dragon_drives[3].motor_on = (data & 0x08) ? 1 : 0;
		}

		/* If we are selecting a different drive, close out this operation */
		if (d != find_drive()) {
			close_sector();
		}

		dragon_disk_haltenable = (data & 0x80) ? 1 : 0;
		break;
	case 8:
		/* command register */
		switch(data) {
		case 0x03:	/* restore */
			set_track(0);
			break;
		case 0x14:	/* seek */
		case 0x17:	/* seek */
			set_track(dragon_disk_datareg);
			break;
		case 0x23:	/* step */
			break;
		case 0x43:	/* step in */
			break;
		case 0x53:	/* step out */
			break;
		case 0x80:	/* read sector */
		case 0xa0:	/* write sector */
			d = find_drive();
			if (!d) {
				dragon_disk_status = 0x80;
				return;
			}
			if (open_sector(d - dragon_drives, d->track, d->sector)) {
				dragon_disk_status = 0x80;
				return;
			}
			dragon_disk_reading = (data == 0x80);
			dragon_disk_writing = (data == 0xa0);
			dragon_disk_status = 0x02;
			break;
		case 0xc0:	/* read address */
			break;
		case 0xe4:	/* read track */
			break;
		case 0xf4:	/* write track */
			break;
		case 0xd0:	/* force interrupt */
			break;
		default:
			if (errorlog) fprintf(errorlog,"Bad disk command: command=%i\n",data);
		}
		break;
	case 9:
		/* track register */
		break;
	case 10:
		/* sector register */
		d = find_drive();
		if (!d) {
			dragon_disk_status = 0x80;
			return;
		}
		d->sector = data;
		break;
	case 11:
		/* data register */
		if (dragon_disk_writing) {
			dragon_disk_status = sector_write(data) ? 0x80 : (dragon_disk_counter ? 0x02 : 0x00);
			return;
		}
		dragon_disk_datareg = data;
		break;
	default:
		if (errorlog) fprintf(errorlog,"Bad disk register write: offset=%i data=%i\n",offset,data);
	}
}

/***************************************************************************
  PIA
***************************************************************************/

int dragon_interrupt(void)
{
	pia_0_cb1_w (0, 1);
	return ignore_interrupt();
}

static void d_pia1_pa_w(int offset, int data)
{
	d_dac = data & 0xfa;
	if (sound_mux)
		DAC_data_w(0,d_dac);
}

static int d_pia0_ca1_r(int offset)
{
	return 0;
}

static int d_pia1_cb1_r(int offset)
{
	return cart_inserted;
}

static void d_pia1_cb2_w(int offset, int data)
{
	sound_mux = data;
}

static void d_pia0_cb2_w(int offset, int data)
{
	joystick = data;
}

static void d_pia1_ca2_w(int offset, int data)
{
	if (tape_motor ^ data)
	{
		/* speed up tape reading */
		coco_ram[0x0093] = 2;
		coco_ram[0x0092] = 3;

		if (data == 0)
		{
			tape_position--;
			tape_position[0] = 0x55; /* insert sync byte */
		}
		tape_motor = data;
	}
}

static void coco_pia1_ca2_w(int offset, int data)
{
	if (tape_motor ^ data)
	{
		/* speed up tape reading */
		coco_ram[0x008f] = 3;
		coco_ram[0x0091] = 2;

#if 0
		if (data == 0)
		{
			tape_position--;
			tape_position[0] = 0x55; /* insert sync byte */
		}
#endif
		tape_motor = data;
	}
}

static void d_pia0_ca2_w(int offset, int data)
{
	joystick_axis = data;
}

static int d_pia0_pa_r(int offset)
{
	int porta=0x7f;

	if ((input_port_0_r(0) | pia0_pb) != 0xff) porta &= ~0x01;
	if ((input_port_1_r(0) | pia0_pb) != 0xff) porta &= ~0x02;
	if ((input_port_2_r(0) | pia0_pb) != 0xff) porta &= ~0x04;
	if ((input_port_3_r(0) | pia0_pb) != 0xff) porta &= ~0x08;
	if ((input_port_4_r(0) | pia0_pb) != 0xff) porta &= ~0x10;
	if ((input_port_5_r(0) | pia0_pb) != 0xff) porta &= ~0x20;
	if ((input_port_6_r(0) | pia0_pb) != 0xff) porta &= ~0x40;
	if (d_dac <= (joystick_axis? input_port_8_r(0): input_port_7_r(0)))
		porta |= 0x80;
	porta &= ~input_port_9_r(0);

	return porta;
}

static int d_pia1_pa_r(int offset)
{
	static int bit=7, bitc=0, *state;
	static int lo[]={1,1,0,0};
	static int hi[]={1,0};

	if (tape_position && tape_motor)
	{
		if (bitc == 0)
		{
			if (bit < 0)
			{
				bit = 7;
				if (tape_position - tape_start < tape_size)
					tape_position++;
			}

			if ((*tape_position >> (7-bit)) & 0x01)
			{
				state = hi;
				bitc = 2;
			}
			else
			{
				state = lo;
				bitc = 4;
			}
			bit--;
		}
		bitc--;
		return (state[bitc]);
	}
	return 1;
}

static void d_pia0_pb_w(int offset, int data)
{
	pia0_pb = data;
}

static void d_pia0_irq_b(int state)
{
	cpu_set_irq_line(0, M6809_IRQ_LINE, state);
}

/***************************************************************************
  Machine Initialization
***************************************************************************/

void coco3_throw_interrupt(int mask)
{
	if (coco3_gimereg[2] & mask)
		cpu_set_irq_line(0, M6809_IRQ_LINE, ASSERT_LINE);
	if (coco3_gimereg[3] & mask)
		cpu_set_irq_line(0, M6809_FIRQ_LINE, ASSERT_LINE);
}

void dragon32_init_machine(void)
{
	pia_config(0, PIA_STANDARD_ORDERING | PIA_8BIT, &dragon_pia_0_intf);
	pia_config(1, PIA_STANDARD_ORDERING | PIA_8BIT, &dragon_pia_1_intf);
	pia_reset();

	coco_rom = memory_region(REGION_CPU1) + 0x8000;
	coco_ram = memory_region(REGION_CPU1);

	/* allow short tape leader */
	coco_rom[0x3dfc] = 4;

	if (cart_inserted)
		cpu_set_irq_line(0, M6809_FIRQ_LINE, ASSERT_LINE);
}

void coco_init_machine(void)
{
	pia_config(0, PIA_STANDARD_ORDERING | PIA_8BIT, &dragon_pia_0_intf);
	pia_config(1, PIA_STANDARD_ORDERING | PIA_8BIT, &coco_pia_1_intf);
	pia_reset();

	coco_rom = memory_region(REGION_CPU1) + 0x10000;
	coco_ram = memory_region(REGION_CPU1);

	/* allow short tape leader */
	coco_rom[0xa791 - 0x8000] = 0xff;

	if (cart_inserted)
		cpu_set_irq_line(0, M6809_FIRQ_LINE, ASSERT_LINE);
	dragon64_enable_64k_w(0, 0);
}


void dragon64_init_machine(void)
{
	dragon32_init_machine();
	coco_rom = memory_region(REGION_CPU1) + 0x10000;
	dragon64_enable_64k_w(0, 0);
}

void coco3_init_machine(void)
{
	int i;

	pia_config(0, PIA_STANDARD_ORDERING | PIA_8BIT, &dragon_pia_0_intf);
	pia_config(1, PIA_STANDARD_ORDERING | PIA_8BIT, &coco_pia_1_intf);
	pia_reset();

	coco_rom = memory_region(REGION_CPU1) + 0x80000;
	coco_ram = memory_region(REGION_CPU1) + 0x70000;
	/* allow short tape leader */
	coco_rom[0xa791 - 0x8000] = 0xff;

	if (cart_inserted)
		coco3_throw_interrupt(1 << 0);

	coco3_enable_64k = 0;
	for (i = 0; i < 7; i++) {
		coco3_mmu[i] = coco3_mmu[i + 8] = 56 + i;
		coco3_gimereg[i] = 0;
	}
	coco3_mmu_update(0, 7);
}

void dragon_stop_machine(void)
{
	if (tape_start) free(tape_start);
	close_sector();
}

