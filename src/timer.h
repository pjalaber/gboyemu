#ifndef _TIMER_H_
#define _TIMER_H_

int32_t timer_init(void);
uint8_t timer_get_counter(void);
uint8_t timer_get_modulo(void);
uint8_t timer_get_control(void);
void timer_set_modulo(uint8_t modulo);
void timer_set_counter(uint8_t counter);
void timer_set_control(uint8_t control);
void timer_update(uint32_t cycles);

int32_t timer_dump(FILE *file);
int32_t timer_restore(FILE *file);

#endif
