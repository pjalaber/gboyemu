#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "gboyemu.h"
#include "timer.h"
#include "interrupt.h"

struct
{
	uint32_t cycles;
	uint8_t counter;
	uint8_t modulo;
#define TIMER_RUN	0x4
#define TIMER_CLOCK	0x3
	uint8_t control;
} timer;

static uint32_t timer_cycles[] = {
	(CLOCK_SPEED_HZ / 4096),
	(CLOCK_SPEED_HZ / 262144),
	(CLOCK_SPEED_HZ / 65536),
	(CLOCK_SPEED_HZ / 16384),
};

int32_t timer_init(void)
{
	memset(&timer, 0, sizeof(timer));
	return 0;
}

uint8_t timer_get_counter(void)
{
	return timer.counter;
}

uint8_t timer_get_modulo(void)
{
	return timer.modulo;
}

uint8_t timer_get_control(void)
{
	return timer.control;
}

void timer_set_modulo(uint8_t modulo)
{
	timer.modulo = modulo;
}

void timer_set_counter(uint8_t counter)
{
	timer.counter = counter;
}

void timer_update(uint32_t cycles)
{
	if ( (timer.control & TIMER_RUN) == 0 )
	{
		timer.cycles = 0;
		return;
	}

	timer.cycles += cycles;
	while ( timer.cycles >= timer_cycles[timer.control & TIMER_CLOCK] )
	{
		timer.counter++;
		if ( timer.counter == 0 )
		{
			timer.counter = timer.modulo;
			interrupt_request(INTERRUPT_TIMER);
		}
		timer.cycles -= timer_cycles[timer.control & TIMER_CLOCK];
	}
}

void timer_set_control(uint8_t control)
{
	timer.control = control;
}

int32_t timer_dump(FILE *file)
{
	if ( fwrite(&timer, 1, sizeof(timer), file) != sizeof(timer) )
		return -1;
	return 0;
}

int32_t timer_restore(FILE *file)
{
	if ( fread(&timer, 1, sizeof(timer), file) != sizeof(timer) )
		return -1;
	return 0;
}


