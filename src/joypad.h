#ifndef _JOYPAD_H_
#define _JOYPAD_H_

int32_t joypad_init(void);

uint8_t joypad_get(void);
void joypad_set(uint8_t value);

uint32_t joypad_handle_key(uint16_t sdlkey, uint32_t keydown);

int32_t joypad_dump(FILE *file);
int32_t joypad_restore(FILE *file);


#endif
