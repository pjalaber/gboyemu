#ifndef _SOUND_H_
#define _SOUND_H_

struct square;

int32_t sound_init(void);

void sound_run(uint32_t cycles);
void sound_start(void);
void sound_stop(void);

int32_t sound_adjust_left_sample_volume(int32_t sample);
int32_t sound_adjust_right_sample_volume(int32_t sample);

uint32_t sound_channel_left_is_on(uint32_t channel);
uint32_t sound_channel_right_is_on(uint32_t channel);

uint8_t sound_adjust_wave_sample_volume(uint8_t wave_sample);

void sound_channel_set_readonly_register_status(uint32_t channel, uint8_t on);
void sound_sweep_shadow(struct square *s);

uint8_t sound_read_NR10(void);
uint8_t sound_read_NR11(void);
uint8_t sound_read_NR12(void);
uint8_t sound_read_NR13(void);
uint8_t sound_read_NR14(void);
uint8_t sound_read_NR21(void);
uint8_t sound_read_NR22(void);
uint8_t sound_read_NR23(void);
uint8_t sound_read_NR24(void);
uint8_t sound_read_NR30(void);
uint8_t sound_read_NR31(void);
uint8_t sound_read_NR32(void);
uint8_t sound_read_NR33(void);
uint8_t sound_read_NR34(void);
uint8_t sound_read_NR41(void);
uint8_t sound_read_NR42(void);
uint8_t sound_read_NR43(void);
uint8_t sound_read_NR44(void);
uint8_t sound_read_NR50(void);
uint8_t sound_read_NR51(void);
uint8_t sound_read_NR52(void);
uint8_t sound_read_wavepattern(uint8_t index);

void sound_write_NR10(uint8_t value8);
void sound_write_NR11(uint8_t value8);
void sound_write_NR12(uint8_t value8);
void sound_write_NR13(uint8_t value8);
void sound_write_NR14(uint8_t value8);
void sound_write_NR21(uint8_t value8);
void sound_write_NR22(uint8_t value8);
void sound_write_NR23(uint8_t value8);
void sound_write_NR24(uint8_t value8);
void sound_write_NR30(uint8_t value8);
void sound_write_NR31(uint8_t value8);
void sound_write_NR32(uint8_t value8);
void sound_write_NR33(uint8_t value8);
void sound_write_NR34(uint8_t value8);
void sound_write_NR41(uint8_t value8);
void sound_write_NR42(uint8_t value8);
void sound_write_NR43(uint8_t value8);
void sound_write_NR44(uint8_t value8);
void sound_write_NR50(uint8_t value8);
void sound_write_NR51(uint8_t value8);
void sound_write_NR52(uint8_t value8);
void sound_write_wavepattern(uint8_t index, uint8_t value8);

int32_t sound_dump(FILE *file);
int32_t sound_restore(FILE *file);

#endif
