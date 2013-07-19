#ifndef _MMU_H_
#define _MMU_H_

int32_t mmu_init(void);
int32_t mmu_dump_bios(const char *filename);

uint8_t mmu_read_mem8(uint16_t addr);
uint16_t mmu_read_mem16(uint16_t addr);

void mmu_write_mem8(uint16_t addr, uint8_t value8);
void mmu_write_mem16(uint16_t addr, uint16_t value16);

int32_t mmu_dump(FILE *file);
int32_t mmu_restore(FILE *file);

#endif
