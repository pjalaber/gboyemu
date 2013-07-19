#ifndef _INTERRUPT_H_
#define _INTERRUPT_H_

int32_t interrupt_init(void);

void interrupt_set_ime(uint8_t state);
uint8_t interrupt_get_ime(void);

void interrupt_set_ie(uint8_t state);
uint8_t interrupt_get_ie(void);

void interrupt_set_flag(uint8_t flag);
uint8_t interrupt_get_flag(void);

/* interrupts
 */
#define INTERRUPT_VBLANK  0x01
#define INTERRUPT_LCDSTAT 0x02
#define INTERRUPT_TIMER   0x04
#define INTERRUPT_SERIAL  0x08
#define INTERRUPT_JOYPAD  0x10
void interrupt_request(uint8_t interrupt);
void interrupt_run(void);

int32_t interrupt_dump(FILE *file);
int32_t interrupt_restore(FILE *file);

#endif
