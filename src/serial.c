#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "serial.h"
#include "interrupt.h"

static struct
{
	uint8_t data;
	uint8_t ctrl;
} serial;

int32_t serial_init(void)
{
	memset(&serial, 0, sizeof(serial));
	return 0;
}

uint8_t serial_read_data(void)
{
	return serial.data;
}

uint8_t serial_read_ctrl(void)
{
	return serial.ctrl;
}

void serial_write_data(uint8_t value8)
{
	serial.data = value8;
}

void serial_write_ctrl(uint8_t value8)
{
	if ( (value8 & 0x80) == 0x80 && (value8 & 0x01) == 0x01 )
	{
		/* no data
		 */
		serial.data = 0xFF;
		serial.ctrl = (value8 & ~0x80);
		interrupt_request(INTERRUPT_SERIAL);
	}
}

int32_t serial_dump(FILE *file)
{
	if ( fwrite(&serial, 1, sizeof(serial), file) != sizeof(serial) )
		return -1;
	return 0;
}

int32_t serial_restore(FILE *file)
{
	if ( fread(&serial, 1, sizeof(serial), file) != sizeof(serial) )
		return -1;
	return 0;
}
