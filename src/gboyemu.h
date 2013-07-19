#ifndef _GBOYEMU_H_
#define _GBOYEMU_H_

#define CLOCK_SPEED_HZ 4194304

int32_t gboyemu_init(void);
void gboyemu_cleanup(void);
int32_t gboyemu_load_rom(const char *rom_filename);
uint32_t gboyemu_run(void);

#endif
