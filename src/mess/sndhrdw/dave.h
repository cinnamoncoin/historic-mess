#ifndef __DAVE_SOUND_CHIP_HEADER_INCLUDED__
#define __DAVE_SOUND_CHIP_HEADER_INCLUDED__

/******************************
DAVE SOUND CHIP
*******************************/

#define DAVE_INT_1KHZ_50HZ_TG	1
#define DAVE_INT_1HZ 2
#define DAVE_INT_INT1 3
#define DAVE_INT_INT2 4


typedef struct DAVE_INTERFACE
{
	void (*reg_r)(int);
	void (*reg_w)(int,int);
} DAVE_INTERFACE;

typedef struct DAVE
{
	unsigned char Regs[32];

	unsigned char Interrupts;
	unsigned char B4_Read;
	unsigned char B4_Write;
	unsigned char FiftyHz;
	unsigned char OneKhz;

	/* int latches */
	unsigned char int_latch;
	/* int enables */
	unsigned char int_enable;
	/* int inputs */
	unsigned char int_input;

	int int_wanted;

	int fiftyhertz;
	int onehz;
} DAVE;

extern int	Dave_sh_start(void);
extern void	Dave_sh_stop(void);
extern void	Dave_sh_update(void);

extern int	Dave_getreg(int);
extern void	Dave_setreg(int,int);

extern int 	Dave_reg_r(int);
extern void	Dave_reg_w(int, int);

extern void	Dave_SetInt(int);

void	Dave_SetIFace(struct DAVE_INTERFACE *newInterface);
int	Dave_Interrupt(void);

#endif
