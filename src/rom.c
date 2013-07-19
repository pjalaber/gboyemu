#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "rom.h"

static struct gb_rom rom;

static const uint8_t scrolling_nintendo_graphics[] =
{
	0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B, 0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
	0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E, 0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99,
	0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC, 0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E
};

int32_t rom_load(const char *filename)
{
	FILE *f;
	uint8_t header[0x150];
	uint32_t i, read_count;
	memset(&rom, 0, sizeof(rom));

	f = fopen(filename, "r");
	if ( f == NULL )
	{
		fprintf(stderr, "Could not open ROM file %s\n", filename);
		return -1;
	}

	if ( fread(header, 1, sizeof(header), f) < sizeof(header) )
	{
		fprintf(stderr, "Error reading ROM header\n");
		return -1;
	}

	if ( memcmp(&header[0x104], scrolling_nintendo_graphics, sizeof(scrolling_nintendo_graphics)) )
	{
		fprintf(stderr, "Wrong ROM header\n");
		return -1;
	}

	for ( i = 0; i < sizeof(rom.title)-1; i++)
		rom.title[i] = tolower(header[0x134+i]);
	fprintf(stderr, "ROM title: %s\n", rom.title);

	/* if ( header[0x143] == 0x80 ) */
	/* { */
	/* 	fprintf(stderr, "Unsupported CGB ROM\n"); */
	/* 	return -1; */
	/* } */

	if ( header[0x146] != 0x00 )
	{
		fprintf(stderr, "Unsupported SGB ROM\n");
		return -1;
	}

	fprintf(stderr, "ROM type: ");
	switch ( header[0x147] )
	{
		case 0x00:
		rom.type = ROM_ONLY;
		fprintf(stderr, "ROM only\n");
		break;

		case 0x01:
		rom.type = ROM_MBC1;
		fprintf(stderr, "ROM+MBC1\n");
		break;

		case 0x02:
		rom.type = ROM_MBC1_RAM;
		fprintf(stderr, "ROM+MBC1+RAM\n");
		break;

		case 0x03:
		rom.type = ROM_MBC1_RAM_BATT;
		fprintf(stderr, "ROM+MBC1+RAM+BATT\n");
		break;

		default:
		fprintf(stderr, "unknown (0x%02X)\n", header[0x147]);
		return -1;
	}

	switch ( header[0x148] )
	{
		case 0x00:
		rom.rom_size = 32*1024;
		break;

		case 0x01:
		rom.rom_size = 64*1024;
		break;

		case 0x02:
		rom.rom_size = 128*1024;
		break;

		case 0x03:
		rom.rom_size = 256*1024;
		break;

		case 0x04:
		rom.rom_size = 512*1024;
		break;

		case 0x05:
		rom.rom_size = 1024*1024;
		break;

		case 0x06:
		rom.rom_size = 2046*1024;
		break;

		case 0x52:
		rom.rom_size = 1125000;
		break;

		case 0x53:
		rom.rom_size = 1250000;
		break;

		case 0x54:
		rom.rom_size = 1500000;
		break;

		default:
		fprintf(stderr, "unknown ROM size\n");
		return -1;
	}

	fprintf(stderr, "ROM size: %u bytes\n", rom.rom_size);

	switch ( header[0x149] )
	{
		case 0x00:
		rom.ram_size = 0;
		break;

		case 0x01:
		rom.ram_size = 2*1024;
		break;

		case 0x02:
		rom.ram_size = 8*1024;
		break;

		case 0x03:
		rom.ram_size = 32*1024;
		break;

		case 0x04:
		rom.ram_size = 128*1024;
		break;

		default:
		fprintf(stderr, "unknown RAM size\n");
		return -1;
	}

	fprintf(stderr, "ROM embedded RAM size: %u bytes\n", rom.ram_size);
	rewind(f);

	switch ( rom.type )
	{
		case ROM_ONLY:
		{
			if ( fread(rom.only.bank, 1,
					ROM_ONLY_BANK_SIZE, f) < sizeof(ROM_ONLY_BANK_SIZE) )
			{
				fprintf(stderr, "Error reading ROM only bank\n");
				fclose(f);
				return -1;
			}
		}
		break;

		case ROM_MBC1:
		case ROM_MBC1_RAM:
		case ROM_MBC1_RAM_BATT:
		{
			while ( !feof(f) )
			{
				read_count = fread(&rom.mbc1.rom.bank[rom.mbc1.rom.bank_count], 1,
					ROM_MBC1_ROM_BANK_SIZE, f);

				if ( read_count == ROM_MBC1_ROM_BANK_SIZE )
				{
					rom.mbc1.rom.bank_count++;
				}
				else if ( !feof(f) )
				{
					fprintf(stderr, "Error reading bank #%d\n", rom.mbc1.rom.bank_count);
					fclose(f);
					return -1;
				}
			}
			fprintf(stderr, "%u ROM bank(s) each %u bytes\n", rom.mbc1.rom.bank_count, ROM_MBC1_ROM_BANK_SIZE);

			if ( rom.type == ROM_MBC1_RAM || rom.type == ROM_MBC1_RAM_BATT )
			{
				assert((rom.ram_size % ROM_MBC1_RAM_BANK_SIZE) == 0);
				rom.mbc1.ram.bank_count = rom.ram_size / ROM_MBC1_RAM_BANK_SIZE;
				fprintf(stderr, "%u RAM bank(s) each %u bytes\n", rom.mbc1.ram.bank_count, ROM_MBC1_RAM_BANK_SIZE);
			}
		}
		break;

		default:
		assert(0);
	}

	return 0;
}

const char *rom_get_title(void)
{
	return rom.title;
}

static inline uint8_t rom_mbc1_bank_translate(uint8_t bank)
{
	switch ( bank )
	{
		case 0x00:
		case 0x20:
		case 0x40:
		case 0x60:
		return bank + 1;

		default:
		return bank;
	}
}

uint8_t rom_get_rom_bank(void)
{
	switch ( rom.type )
	{
		case ROM_ONLY:
		return 0;

		case ROM_MBC1_RAM_BATT:
		case ROM_MBC1_RAM:
		case ROM_MBC1:
		return rom_mbc1_bank_translate(rom.mbc1.rom.bank_cur);

		default:
		assert(0);
		return 0;
	}
}

void rom_write_rom8(uint16_t addr, uint8_t value8)
{
	assert(addr <= 0x7FFF);

	switch ( rom.type )
	{
		case ROM_ONLY:
		fprintf(stderr, "invalid write to rom at 0x%04X\n", addr);
		break;

		case ROM_MBC1_RAM_BATT:
		case ROM_MBC1_RAM:
		case ROM_MBC1:
		if ( addr <= 0x1FFF )
		{
			/* RAM enable/disable
			 */
			rom.mbc1.ram.enabled = ((value8 & 0x0F) == 0x0A);
			return;
		}
		else if ( addr <= 0x3FFF )
		{
			/* 5 lower bits of rom bank number
			 */
			rom.mbc1.bank_info = (rom.mbc1.bank_info & 0xE0) | (value8 & 0x1F);
		}
		else if ( addr <= 0x5FFF )
		{
			/* 2 bits ram bank number or 2 upper bits of rom bank number
			 */
			rom.mbc1.bank_info = ((value8 & 0x3) << 5) | (rom.mbc1.bank_info & 0x9F);
		}
		else
		{
			/* ROM/RAM mode selection
			 */
			rom.mbc1.bank_info = (rom.mbc1.bank_info & 0xEF) | ((value8 & 0x1) << 7);
		}

		if ( (rom.mbc1.bank_info & 0x80) == 0 )
		{
			rom.mbc1.rom.bank_cur = rom.mbc1.bank_info;
			rom.mbc1.ram.bank_cur = 0;
		}
		else
		{
			rom.mbc1.rom.bank_cur = rom.mbc1.bank_info & 0x1F;
			rom.mbc1.ram.bank_cur = (rom.mbc1.bank_info >> 5) & 0x3;
		}
		break;

		default:
		assert(0);
	}
}

uint8_t rom_read_rom8(uint16_t addr)
{
	uint8_t bank;
	assert(addr <= 0x7FFF);
	switch ( rom.type )
	{
		case ROM_ONLY:
		return rom.only.bank[addr];
		break;

		case ROM_MBC1_RAM_BATT:
		case ROM_MBC1_RAM:
		case ROM_MBC1:
		if ( addr <= 0x3FFF )
			return rom.mbc1.rom.bank[0][addr];
		else
		{
			bank = rom_mbc1_bank_translate(rom.mbc1.rom.bank_cur);
			assert(bank < rom.mbc1.rom.bank_count);
			return rom.mbc1.rom.bank[bank][addr - 0x4000];
		}
		break;

		default:
		assert(0);
		return 0;
	}
}

uint8_t rom_read_ram8(uint16_t addr)
{
	assert(addr <= ROM_MBC1_RAM_BANK_SIZE - 1);
	switch ( rom.type )
	{
		case ROM_ONLY:
		case ROM_MBC1:
		fprintf(stderr, "Invalid RAM read: no RAM\n");
		return 0;
		break;

		case ROM_MBC1_RAM_BATT:
		case ROM_MBC1_RAM:
		if ( rom.mbc1.ram.enabled == 0 )
		{
			fprintf(stderr, "Invalid RAM read: RAM not enabled\n");
			return 0;
		}
		assert(rom.mbc1.ram.bank_cur < rom.mbc1.ram.bank_count);
		return rom.mbc1.ram.bank[rom.mbc1.ram.bank_cur][addr];
		break;

		default:
		assert(0);
		return 0;

	}
}

void rom_write_ram8(uint16_t addr, uint8_t value8)
{
	assert(addr <= ROM_MBC1_RAM_BANK_SIZE - 1);
	switch ( rom.type )
	{
		case ROM_ONLY:
		case ROM_MBC1:
		/* no available RAM on cartridge
		 */
		break;

		case ROM_MBC1_RAM_BATT:
		case ROM_MBC1_RAM:
		if ( rom.mbc1.ram.enabled == 0 )
		{
			fprintf(stderr, "Invalid RAM write: RAM not enabled\n");
		}
		else
		{
			assert(rom.mbc1.ram.bank_cur < rom.mbc1.ram.bank_count);
			rom.mbc1.ram.bank[rom.mbc1.ram.bank_cur][addr] = value8;
		}
		break;

		default:
		assert(0);
	}
}

int32_t rom_dump(FILE *file)
{
	if ( fwrite(&rom, 1, sizeof(rom), file) != sizeof(rom) )
		return -1;
	return 0;
}

int32_t rom_restore(FILE *file)
{
	if ( fread(&rom, 1, sizeof(rom), file) != sizeof(rom) )
		return -1;
	return 0;
}
