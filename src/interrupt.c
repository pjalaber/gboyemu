#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include "interrupt.h"
#include "z80.h"
#include "mmu.h"

static struct
{
	/* interrupt master flag. 0 or 1.
	 */
	uint8_t ime;

	/* interrupt enable bits
	 */
	uint8_t ie;

	/* interrupt flag
	 * same value as ie.
	 */
	uint8_t flag;
} interrupt;

int32_t interrupt_init(void)
{
	memset(&interrupt, 0, sizeof(interrupt));
	return 0;
}

void interrupt_set_ime(uint8_t ime)
{
	interrupt.ime = ime;
}

uint8_t interrupt_get_ime(void)
{
	return interrupt.ime;
}

void interrupt_set_flag(uint8_t flag)
{
	interrupt.flag = flag;
}

uint8_t interrupt_get_flag(void)
{
	return interrupt.flag;
}

void interrupt_set_ie(uint8_t ie)
{
	interrupt.ie = ie;
}

uint8_t interrupt_get_ie(void)
{
	return interrupt.ie;
}

void interrupt_request(uint8_t interrupt_type)
{
	interrupt.flag |= interrupt_type;
}

void interrupt_run(void)
{
	if ( (interrupt.flag & INTERRUPT_VBLANK) == INTERRUPT_VBLANK &&
		(interrupt.ie & INTERRUPT_VBLANK) == INTERRUPT_VBLANK )
	{
		if ( interrupt.ime )
		{
			interrupt.flag &= ~INTERRUPT_VBLANK;
			interrupt.ime = 0;
			z80_call(0x40);
		}

		z80_resume_halt();
	}
	else if ( (interrupt.flag & INTERRUPT_LCDSTAT) == INTERRUPT_LCDSTAT &&
		(interrupt.ie & INTERRUPT_LCDSTAT) == INTERRUPT_LCDSTAT )
	{
		if ( interrupt.ime )
		{
			interrupt.flag &= ~INTERRUPT_LCDSTAT;
			interrupt.ime = 0;
			z80_call(0x48);
		}

		z80_resume_halt();
	}
	else if ( (interrupt.flag & INTERRUPT_TIMER) == INTERRUPT_TIMER &&
		(interrupt.ie & INTERRUPT_TIMER) == INTERRUPT_TIMER )
	{
		if ( interrupt.ime )
		{
			interrupt.flag &= ~INTERRUPT_TIMER;
			interrupt.ime = 0;
			z80_call(0x50);
		}

		z80_resume_halt();
	}
	else if ( (interrupt.flag & INTERRUPT_SERIAL) == INTERRUPT_SERIAL &&
		(interrupt.ie & INTERRUPT_SERIAL) == INTERRUPT_SERIAL )
	{
		if ( interrupt.ime )
		{
			interrupt.flag &= ~INTERRUPT_SERIAL;
			interrupt.ime = 0;
			z80_call(0x58);
		}

		z80_resume_halt();
	}
	else if ( (interrupt.flag & INTERRUPT_JOYPAD) == INTERRUPT_JOYPAD &&
		(interrupt.ie & INTERRUPT_JOYPAD) == INTERRUPT_JOYPAD )
	{
		if ( interrupt.ime )
		{
			interrupt.flag &= ~INTERRUPT_JOYPAD;
			interrupt.ime = 0;
			z80_call(0x60);
		}

		z80_resume_halt();
	}
}

int32_t interrupt_dump(FILE *file)
{
	if ( fwrite(&interrupt, 1, sizeof(interrupt), file) != sizeof(interrupt) )
		return -1;
	return 0;
}

int32_t interrupt_restore(FILE *file)
{
	if ( fread(&interrupt, 1, sizeof(interrupt), file) != sizeof(interrupt) )
		return -1;
	return 0;
}
