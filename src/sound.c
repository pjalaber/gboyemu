#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <SDL.h>
#include <SDL_audio.h>
#include "gboyemu.h"
#include "sound.h"
#include "square.h"
#include "lfsr.h"
#include "blip_buf.h"

#define FRAC_SECOND(f) (CLOCK_SPEED_HZ / (f))

static uint32_t debug_sound = 0;

static struct
{
	uint8_t NR10, NR11, NR12, NR13, NR14,
		NR21, NR22, NR23, NR24, NR30,
		NR31, NR32, NR33, NR34, NR41,
		NR42, NR43, NR44, NR50, NR51,
		NR52;
#define WAVEPATTERN_SIZE 16
	uint8_t wavepattern[WAVEPATTERN_SIZE];
} sound;


static SDL_AudioSpec sdl_desired, sdl_obtained;

#define CH1_SWEEP_TIME	       ((sound.NR10 >> 4) & 0x7)
#define CH1_SWEEP_DIRECTION    ((sound.NR10 >> 3) & 0x1)
#define CH1_SWEEP_SHIFT	       (sound.NR10 & 0x7)
#define CH1_DUTY	       ((sound.NR11 >> 6) & 0x3)
#define CH1_SOUND_LENGTH       (sound.NR11 & 0x3F)
#define CH1_ENVELOPE_VOLUME    ((sound.NR12 >> 4) & 0xF)
#define CH1_ENVELOPE_DIRECTION ((sound.NR12 >> 3) & 0x1)
#define CH1_ENVELOPE_SWEEP     (sound.NR12 & 0x7)
#define CH1_FREQUENCY	       (((sound.NR14 & 0x7) << 8) | sound.NR13)
#define CH1_TRIGGER	       ((sound.NR14 & 0x80) >> 7)
#define CH1_SOUND_LENGTH_ON    (sound.NR14 & 0x40)
#define CH1_WRITE_FREQUENCY(f)						\
	do {								\
		sound.NR13 = (f) & 0xFF;				\
		sound.NR14 = (sound.NR14 & 0xF8) | (((f) >> 8) & 0x7);	\
	} while (0)

#define CH2_DUTY	       ((sound.NR21 >> 6) & 0x3)
#define CH2_SOUND_LENGTH       (sound.NR21 & 0x3F)
#define CH2_ENVELOPE_VOLUME    ((sound.NR22 >> 4) & 0xF)
#define CH2_ENVELOPE_DIRECTION ((sound.NR22 >> 3) & 0x1)
#define CH2_ENVELOPE_SWEEP     (sound.NR22 & 0x7)
#define CH2_FREQUENCY	       (((sound.NR24 & 0x7) << 8) | sound.NR23)
#define CH2_TRIGGER	       ((sound.NR24 & 0x80) >> 7)
#define CH2_SOUND_LENGTH_ON    (sound.NR24 & 0x40)

#define CH3_SOUND_ON	       (sound.NR30 & 0x80)
#define CH3_SOUND_LENGTH       (sound.NR31)
#define CH3_OUTPUT_LEVEL       ((sound.NR32 >> 5) & 0x3)
#define CH3_FREQUENCY	       (((sound.NR34 & 0x7) << 8) | sound.NR33)
#define CH3_TRIGGER	       ((sound.NR34 & 0x80) >> 7)
#define CH3_SOUND_LENGTH_ON    (sound.NR34 & 0x40)

#define CH4_SOUND_LENGTH       (sound.NR41 & 0x3F)
#define CH4_ENVELOPE_VOLUME    ((sound.NR42 >> 4) & 0xF)
#define CH4_ENVELOPE_DIRECTION ((sound.NR42 >> 3) & 0x1)
#define CH4_ENVELOPE_SWEEP     (sound.NR42 & 0x7)
#define CH4_SHIFT_CLOCK_FREQ   ((sound.NR43 >> 4) & 0xF)
#define CH4_COUNTER_WIDTH      ((sound.NR43 >> 3) & 0x1)
#define CH4_DIV_RATIO	       (sound.NR43 & 0x7)
#define CH4_TRIGGER	       ((sound.NR44 & 0x80) >> 7)
#define CH4_SOUND_LENGTH_ON    (sound.NR44 & 0x40)

#define CTRL_LEFT_VOLUME       ((sound.NR50 >> 4) & 0x7)
#define CTRL_RIGHT_VOLUME      (sound.NR50 & 0x7)
#define CTRL_CH4_LEFT_ON       (sound.NR51 & 0x80)
#define CTRL_CH3_LEFT_ON       (sound.NR51 & 0x40)
#define CTRL_CH2_LEFT_ON       (sound.NR51 & 0x20)
#define CTRL_CH1_LEFT_ON       (sound.NR51 & 0x10)
#define CTRL_CH4_RIGHT_ON      (sound.NR51 & 0x8)
#define CTRL_CH3_RIGHT_ON      (sound.NR51 & 0x4)
#define CTRL_CH2_RIGHT_ON      (sound.NR51 & 0x2)
#define CTRL_CH1_RIGHT_ON      (sound.NR51 & 0x1)

#define CTRL_SOUND_ON	       (sound.NR52 & 0x80)

struct {
	struct square ch1square, ch2square;
	struct wave ch3wave;
	struct noise ch4noise;
	blip_t *blip_left, *blip_right;
} signal;

static void sound_callback(void *userdata, uint8_t *stream, int32_t len);

int32_t sound_init(void)
{
	memset(&sound, 0, sizeof(sound));
	memset(&signal, 0, sizeof(signal));

	lfsr_init();

	square_init(&signal.ch1square, 1);
	square_init(&signal.ch2square, 2);
	wave_init(&signal.ch3wave, sound.wavepattern, WAVEPATTERN_SIZE*2, 3);
	noise_init(&signal.ch4noise, 4);

	sdl_desired.freq = 44100;
	sdl_desired.format = AUDIO_S16SYS;
	sdl_desired.channels = 2;
	sdl_desired.samples = 2048;
	sdl_desired.callback = sound_callback;
	sdl_desired.userdata = NULL;

	if ( SDL_OpenAudio(&sdl_desired, &sdl_obtained) < 0 )
	{
		fprintf(stderr, "Couldn't open audio: %s\n", SDL_GetError());
		return -1;
	}

	signal.blip_left = blip_new(sdl_obtained.freq / 10);
	if ( signal.blip_left == NULL )
		return -1;
	blip_set_rates(signal.blip_left, CLOCK_SPEED_HZ, sdl_obtained.freq);

	signal.blip_right = blip_new(sdl_obtained.freq / 10);
	if ( signal.blip_right == NULL )
	{
		blip_delete(signal.blip_left);
		return -1;
	}
	blip_set_rates(signal.blip_right, CLOCK_SPEED_HZ, sdl_obtained.freq);

	fprintf(stderr, "Audio: freq=%u samples=%u\n", sdl_obtained.freq, sdl_obtained.samples);
	return 0;
}

void sound_start(void)
{
	SDL_PauseAudio(0);
}

void sound_stop(void)
{
	SDL_PauseAudio(1);
}

int32_t sound_adjust_left_sample_volume(int32_t sample)
{
	assert(CTRL_LEFT_VOLUME <= 7);
	return (sample / 7) * CTRL_LEFT_VOLUME;
}

int32_t sound_adjust_right_sample_volume(int32_t sample)
{
	assert(CTRL_RIGHT_VOLUME <= 7);
	return (sample / 7) * CTRL_RIGHT_VOLUME;
}

uint8_t sound_adjust_wave_sample_volume(uint8_t wave_sample)
{
	/* wave samples are coded on 4 bits
	 */
	assert(wave_sample <= 15);

	switch ( CH3_OUTPUT_LEVEL )
	{
		case 0:
		wave_sample >>= 4;
		break;

		case 1:
		wave_sample >>= 0;
		break;

		case 2:
		wave_sample >>= 1;
		break;

		case 3:
		wave_sample >>= 2;
		break;
	}

	return wave_sample;
}

uint32_t sound_channel_left_is_on(uint32_t channel)
{
	switch ( channel )
	{
		case 1:
		return CTRL_CH1_LEFT_ON;

		case 2:
		return CTRL_CH2_LEFT_ON;

		case 3:
		return CTRL_CH3_LEFT_ON;

		case 4:
		return CTRL_CH4_LEFT_ON;

		default:
		assert(0);
		return 0;
	}
}

uint32_t sound_channel_right_is_on(uint32_t channel)
{
	switch ( channel )
	{
		case 1:
		return CTRL_CH1_RIGHT_ON;

		case 2:
		return CTRL_CH2_RIGHT_ON;

		case 3:
		return CTRL_CH3_RIGHT_ON;

		case 4:
		return CTRL_CH4_RIGHT_ON;

		default:
		assert(0);
		return 0;
	}
}

void sound_sweep_shadow(struct square *s)
{
	CH1_WRITE_FREQUENCY(s->sweep.shadow_freq);
	assert(CH1_FREQUENCY == s->sweep.shadow_freq);

	s->period.tot_clocks = (2048 - s->sweep.shadow_freq) * 4;
	square_sweep_shadow(s);
}

void sound_channel_set_readonly_register_status(uint32_t channel, uint8_t on)
{
	assert(channel >= 1 && channel <= 4);
	if ( on )
		sound.NR52 |= (1 << (channel - 1));
	else
		sound.NR52 &= ~(1 << (channel - 1));
}

static void sound_callback(void *userdata, uint8_t *stream, int32_t len)
{
	int16_t *buffer = (int16_t *)stream;
	uint32_t count = len / (sizeof(int16_t) * 2);
	/* int32_t missing; */
	/* uint32_t clocks; */

	/* missing = count - blip_samples_avail(signal.blip_left); */
	/* if ( missing > 0 ) */
	/* { */
	/* 	clocks = blip_clocks_needed(signal.blip_left, missing); */
	/* 	fprintf(stderr, "left: missing %u clocks\n", clocks); */
	/* } */

	/* missing = count - blip_samples_avail(signal.blip_right); */
	/* if ( missing > 0 ) */
	/* { */
	/* 	clocks = blip_clocks_needed(signal.blip_right, missing); */
	/* 	fprintf(stderr, "right: missing %u clocks\n", clocks); */
	/* } */

	blip_read_samples(signal.blip_left, buffer, count, 1);
	blip_read_samples(signal.blip_right, buffer + 1, count, 1);
}

void sound_run(uint32_t cycles)
{

	SDL_LockAudio();

	square_run(&signal.ch1square, signal.blip_left, signal.blip_right, cycles);
	square_run(&signal.ch2square, signal.blip_left, signal.blip_right, cycles);
	if ( CH3_SOUND_ON )
		wave_run(&signal.ch3wave, signal.blip_left, signal.blip_right, cycles);
	noise_run(&signal.ch4noise, signal.blip_left, signal.blip_right, cycles);

	blip_end_frame(signal.blip_left, cycles);
	blip_end_frame(signal.blip_right, cycles);

	SDL_UnlockAudio();
}

uint8_t sound_read_NR10(void)
{
	return sound.NR10;
}

uint8_t sound_read_NR11(void)
{
	return sound.NR11;
}

uint8_t sound_read_NR12(void)
{
	return sound.NR12;
}

uint8_t sound_read_NR13(void)
{
	return sound.NR13;
}

uint8_t sound_read_NR14(void)
{
	return sound.NR14;
}

uint8_t sound_read_NR21(void)
{
	return sound.NR21;
}

uint8_t sound_read_NR22(void)
{
	return sound.NR22;
}

uint8_t sound_read_NR23(void)
{
	return sound.NR23;
}

uint8_t sound_read_NR24(void)
{
	return sound.NR24;
}

uint8_t sound_read_NR30(void)
{
	return sound.NR30;
}

uint8_t sound_read_NR31(void)
{
	return sound.NR31;
}

uint8_t sound_read_NR32(void)
{
	return sound.NR32;
}

uint8_t sound_read_NR33(void)
{
	return sound.NR33;
}

uint8_t sound_read_NR34(void)
{
	return sound.NR34;
}

uint8_t sound_read_NR41(void)
{
	return sound.NR41;
}

uint8_t sound_read_NR42(void)
{
	return sound.NR42;
}

uint8_t sound_read_NR43(void)
{
	return sound.NR43;
}

uint8_t sound_read_NR44(void)
{
	return sound.NR44;
}

uint8_t sound_read_NR50(void)
{
	return sound.NR50;
}

uint8_t sound_read_NR51(void)
{
	return sound.NR51;
}

uint8_t sound_read_NR52(void)
{
	return sound.NR52;
}

uint8_t sound_read_wavepattern(uint8_t index)
{
	assert(index < WAVEPATTERN_SIZE);
	return sound.wavepattern[index];
}

void sound_write_NR10(uint8_t value8)
{
	if ( debug_sound )
		fprintf(stderr, "sound write NR10\n");
	sound.NR10 = value8;
	signal.ch1square.sweep.direction = (CH1_SWEEP_DIRECTION == 0) ? SWEEP_INC : SWEEP_DEC;
	signal.ch1square.sweep.shift = CH1_SWEEP_SHIFT;
	signal.ch1square.sweep.tot_clocks = CH1_SWEEP_TIME * FRAC_SECOND(128);
}

void sound_write_NR11(uint8_t value8)
{
	if ( debug_sound )
		fprintf(stderr, "sound write NR11\n");
	sound.NR11 = value8;
	signal.ch1square.period.waveform = CH1_DUTY;
	signal.ch1square.length.counter = 64 - CH1_SOUND_LENGTH;
}

void sound_write_NR12(uint8_t value8)
{
	if ( debug_sound )
		fprintf(stderr, "sound write NR12\n");
	sound.NR12 = value8;
	signal.ch1square.envelope.counter = CH1_ENVELOPE_VOLUME;
	signal.ch1square.envelope.tot_clocks = CH1_ENVELOPE_SWEEP * FRAC_SECOND(64);
	signal.ch1square.envelope.direction = (CH1_ENVELOPE_DIRECTION == 0) ? ENVELOPE_DEC : ENVELOPE_INC;
}

void sound_write_NR13(uint8_t value8)
{
	if ( debug_sound )
		fprintf(stderr, "sound write NR13\n");
	sound.NR13 = value8;
	signal.ch1square.period.tot_clocks = (2048 - CH1_FREQUENCY) * 4;
}

void sound_write_NR14(uint8_t value8)
{
	if ( debug_sound )
		fprintf(stderr, "sound write NR14\n");
	sound.NR14 = value8;

       	signal.ch1square.period.tot_clocks = (2048 - CH1_FREQUENCY) * 4;
	signal.ch1square.length.stop_on_expire = CH1_SOUND_LENGTH_ON;
	signal.ch1square.length.tot_clocks = FRAC_SECOND(64);
	if ( CH1_TRIGGER )
	{
		sound_channel_set_readonly_register_status(1, 1);
		signal.ch1square.envelope.counter = CH1_ENVELOPE_VOLUME;
		signal.ch1square.is_disabled = 0;
		signal.ch1square.sweep.shadow_freq = CH1_FREQUENCY;
		signal.ch1square.sweep.cur_clocks = 0;
		if ( signal.ch1square.length.counter == 0 )
			signal.ch1square.length.counter = 64;
		if ( signal.ch1square.sweep.shift > 0 && signal.ch1square.sweep.tot_clocks > 0 )
			sound_sweep_shadow(&signal.ch1square);
	}
}

void sound_write_NR21(uint8_t value8)
{
	if ( debug_sound )
		fprintf(stderr, "sound write NR21\n");
	sound.NR21 = value8;
	signal.ch2square.period.waveform = CH2_DUTY;
	signal.ch2square.length.counter = 64 - CH2_SOUND_LENGTH;
}

void sound_write_NR22(uint8_t value8)
{
	if ( debug_sound )
		fprintf(stderr, "sound write NR22\n");
	sound.NR22 = value8;
	signal.ch2square.envelope.counter = CH2_ENVELOPE_VOLUME;
	signal.ch2square.envelope.tot_clocks = CH2_ENVELOPE_SWEEP * FRAC_SECOND(64);
	signal.ch2square.envelope.direction = (CH2_ENVELOPE_DIRECTION == 0) ? ENVELOPE_DEC : ENVELOPE_INC;
}

void sound_write_NR23(uint8_t value8)
{
	if ( debug_sound )
		fprintf(stderr, "sound write NR23\n");
	sound.NR23 = value8;
	signal.ch2square.period.tot_clocks = (2048 - CH2_FREQUENCY) * 4;
}

void sound_write_NR24(uint8_t value8)
{
	if ( debug_sound )
		fprintf(stderr, "sound write NR24\n");
	sound.NR24 = value8;
	signal.ch2square.period.tot_clocks = (2048 - CH2_FREQUENCY) * 4;
	signal.ch2square.length.stop_on_expire = CH2_SOUND_LENGTH_ON;
	signal.ch2square.length.tot_clocks = FRAC_SECOND(64);
	if ( CH2_TRIGGER )
	{
		sound_channel_set_readonly_register_status(2, 1);
		signal.ch2square.envelope.counter = CH2_ENVELOPE_VOLUME;
		signal.ch2square.is_disabled = 0;
		if ( signal.ch2square.length.counter == 0 )
			signal.ch2square.length.counter = 64;
	}
}

void sound_write_NR30(uint8_t value8)
{
	if ( debug_sound )
		fprintf(stderr, "sound write NR30\n");
	sound.NR30 = value8;
}

void sound_write_NR31(uint8_t value8)
{
	if ( debug_sound )
		fprintf(stderr, "sound write NR31\n");
	sound.NR31 = value8;
	signal.ch3wave.length.counter = 256 - CH3_SOUND_LENGTH;
}

void sound_write_NR32(uint8_t value8)
{
	if ( debug_sound )
		fprintf(stderr, "sound write NR32\n");
	sound.NR32 = value8;
}

void sound_write_NR33(uint8_t value8)
{
	if ( debug_sound )
		fprintf(stderr, "sound write NR33\n");
	sound.NR33 = value8;
	signal.ch3wave.period.tot_clocks = (2048 - CH3_FREQUENCY) * 2;
}

void sound_write_NR34(uint8_t value8)
{
	if ( debug_sound )
		fprintf(stderr, "sound write NR34\n");
	sound.NR34 = value8;
	signal.ch3wave.length.stop_on_expire = CH3_SOUND_LENGTH_ON;
	signal.ch3wave.length.tot_clocks = FRAC_SECOND(256);
	signal.ch3wave.period.tot_clocks = (2048 - CH3_FREQUENCY) * 2;
	if ( CH3_TRIGGER )
	{
		sound_channel_set_readonly_register_status(3, 1);
		signal.ch3wave.period.cur_clocks = 0;
		signal.ch3wave.wave.pos = 0;
		signal.ch3wave.is_disabled = 0;
		if ( signal.ch3wave.length.counter == 0 )
			signal.ch3wave.length.counter = 256;
	}
}

void sound_write_NR41(uint8_t value8)
{
	if ( debug_sound )
		fprintf(stderr, "sound write NR41\n");
	sound.NR41 = value8;
	signal.ch4noise.length.counter = 64 - CH4_SOUND_LENGTH;
}

void sound_write_NR42(uint8_t value8)
{
	if ( debug_sound )
		fprintf(stderr, "sound write NR42\n");
	sound.NR42 = value8;
	signal.ch4noise.envelope.counter = CH4_ENVELOPE_VOLUME;
	signal.ch4noise.envelope.tot_clocks = CH4_ENVELOPE_SWEEP * FRAC_SECOND(64);
	signal.ch4noise.envelope.direction = (CH4_ENVELOPE_DIRECTION == 0) ? ENVELOPE_DEC : ENVELOPE_INC;
}

void sound_write_NR43(uint8_t value8)
{
	if ( debug_sound )
		fprintf(stderr, "sound write NR43\n");
	sound.NR43 = value8;
	signal.ch4noise.period.tot_clocks = ((CH4_DIV_RATIO + 1) << (CH4_SHIFT_CLOCK_FREQ + 1)) * 4;
	signal.ch4noise.lfsr.width = (CH4_COUNTER_WIDTH) ? LFSR7 : LFSR15;
	if ( signal.ch4noise.lfsr.width == LFSR7 && signal.ch4noise.lfsr.counter >= 127 )
		signal.ch4noise.lfsr.counter = 0;
}

void sound_write_NR44(uint8_t value8)
{
	if ( debug_sound )
		fprintf(stderr, "sound write NR44\n");
	sound.NR44 = value8;
	signal.ch4noise.length.stop_on_expire = CH4_SOUND_LENGTH_ON;
	signal.ch4noise.length.tot_clocks = FRAC_SECOND(64);
	if ( CH4_TRIGGER )
	{
		sound_channel_set_readonly_register_status(4, 1);
		signal.ch4noise.lfsr.counter = 0;
		signal.ch4noise.envelope.counter = CH4_ENVELOPE_VOLUME;
		signal.ch4noise.is_disabled = 0;
		if ( signal.ch4noise.length.counter == 0 )
			signal.ch4noise.length.counter = 64;
	}
}

void sound_write_NR50(uint8_t value8)
{
	if ( debug_sound )
		fprintf(stderr, "sound write NR50\n");
	sound.NR50 = value8;
}

void sound_write_NR51(uint8_t value8)
{
	if ( debug_sound )
		fprintf(stderr, "sound write NR51\n");
	sound.NR51 = value8;
}

void sound_write_NR52(uint8_t value8)
{
	if ( debug_sound )
		fprintf(stderr, "sound write NR52\n");
	if ( (value8 & 0x80) == 0 )
	{
		memset(&sound, 0, sizeof(sound));
	}
	else
		sound.NR52 |= 0x80;
}

void sound_write_wavepattern(uint8_t index, uint8_t value8)
{
	assert(index < WAVEPATTERN_SIZE);
	if ( debug_sound )
		fprintf(stderr, "sound write wavepattern [%u]\n", index);
	sound.wavepattern[index] = value8;
}

int32_t sound_dump(FILE *file)
{
	if ( fwrite(&sound, 1, sizeof(sound), file) != sizeof(sound) )
		return -1;
	return 0;
}

int32_t sound_restore(FILE *file)
{
	if ( fread(&sound, 1, sizeof(sound), file) != sizeof(sound) )
		return -1;
	return 0;
}
