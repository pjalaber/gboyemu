#ifndef _SERIAL_H_
#define _SERIAL_H_

int32_t serial_init(void);

uint8_t serial_read_data(void);
uint8_t serial_read_ctrl(void);

void serial_write_data(uint8_t value8);
void serial_write_ctrl(uint8_t value8);

int32_t serial_dump(FILE *file);
int32_t serial_restore(FILE *file);

#endif
