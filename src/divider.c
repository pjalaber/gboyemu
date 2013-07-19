#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "gboyemu.h"
#include "divider.h"

#define DIVIDER_CYCLES (CLOCK_SPEED_HZ / 16384)

static struct
{
	uint32_t cycles;
	uint8_t counter;
} divider;

int32_t divider_init(void)
{
	memset(&divider, 0, sizeof(divider));
	return 0;
}

uint8_t divider_get_counter(void)
{
	return divider.counter;
}

void divider_set_counter(uint8_t counter)
{
	/* writing any value to divider register
	 * resets it to 0.
	 */
	divider.counter = 0;
}

void divider_update(uint32_t cycles)
{
	divider.cycles += cycles;
	while ( divider.cycles >= DIVIDER_CYCLES )
	{
		divider.cycles -= DIVIDER_CYCLES;
		divider.counter++;
	}
}

int32_t divider_dump(FILE *file)
{
	if ( fwrite(&divider, 1, sizeof(divider), file) != sizeof(divider) )
		return -1;
	return 0;
}

int32_t divider_restore(FILE *file)
{
	if ( fread(&divider, 1, sizeof(divider), file) != sizeof(divider) )
		return -1;
	return 0;
}
