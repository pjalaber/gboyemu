#include <stdio.h>
#include <stdint.h>
#include "lfsr.h"

uint8_t lfsr7_table[127];
uint8_t lfsr15_table[32767];

void lfsr_init(void)
{
	uint32_t i;
	uint16_t value, high_bit;

	value = 0x7F;
	for ( i = 0; i < 127; i++ )
	{
		lfsr7_table[i] = value & 0x1;
		high_bit = (value & 0x1) ^ ((value >> 1) & 0x1);
		value = (high_bit << 6) | (value >> 1);
	}

	value = 0x7FFF;
	for ( i = 0; i < 32767; i++ )
	{
		lfsr15_table[i] = value & 0x1;
		high_bit = (value & 0x1) ^ ((value >> 1) & 0x1);
		value = (high_bit << 14) | (value >> 1);
	}
}
