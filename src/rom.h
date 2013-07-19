#ifndef _ROM_H_
#define _ROM_H_

enum rom_type
{
	ROM_ONLY,
	ROM_MBC1,
	ROM_MBC1_RAM,
	ROM_MBC1_RAM_BATT,
};

struct gb_rom
{
	enum rom_type type;
	char title[16];
	uint32_t rom_size, ram_size;
	union
	{
		struct
		{
#define ROM_ONLY_BANK_SIZE (32 * 1024)
			uint8_t bank[ROM_ONLY_BANK_SIZE];
		} only;
		struct
		{
			/* bit 7:   ROM/RAM mode selection ( 0 = ROM, 1 = RAM)
			 * bit 6-5: RAM bank number or ROM bank number upper bits
			 * bit 4-0: ROM bank number lower bits
			 */
			uint8_t bank_info;
			struct {
				uint8_t bank_cur;
				uint8_t bank_count;
#define ROM_MBC1_ROM_BANK_SIZE (16 * 1024)
				uint8_t bank[126][ROM_MBC1_ROM_BANK_SIZE];
			} rom;
			struct
			{
				uint8_t enabled;
				uint8_t bank_count;
				uint8_t bank_cur;
#define ROM_MBC1_RAM_BANK_SIZE (8 * 1024)
				uint8_t bank[4][ROM_MBC1_RAM_BANK_SIZE];
			} ram;
		} mbc1;
	};
};

int32_t rom_load(const char *filename);
const char *rom_get_title(void);

uint8_t rom_get_rom_bank(void);

uint8_t rom_read_rom8(uint16_t addr);
void rom_write_rom8(uint16_t addr, uint8_t value8);

uint8_t rom_read_ram8(uint16_t addr);
void rom_write_ram8(uint16_t addr, uint8_t value8);

int32_t rom_dump(FILE *file);
int32_t rom_restore(FILE *file);

#endif
