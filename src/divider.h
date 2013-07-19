#ifndef _DIVIDER_H_
#define _DIVIDER_H_

int32_t divider_init(void);
uint8_t divider_get(void);
void divider_update(uint32_t cycles);
uint8_t divider_get_counter(void);
void divider_set_counter(uint8_t counter);

int32_t divider_dump(FILE *file);
int32_t divider_restore(FILE *file);

#endif
