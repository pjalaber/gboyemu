#ifndef _SQUARE_H_
#define _SQUARE_H_

#include "blip_buf.h"

enum envelope_dir { ENVELOPE_INC, ENVELOPE_DEC };

struct square
{
	struct
	{
		uint32_t cur_clocks;
		uint32_t tot_clocks;
		uint8_t counter;
		uint8_t waveform;
	} period;

	struct
	{
		uint32_t shadow_freq;
		uint32_t cur_clocks;
		uint32_t tot_clocks;
		uint8_t shift;
		enum { SWEEP_INC, SWEEP_DEC } direction;
	} sweep;

	struct
	{
		uint32_t cur_clocks;
		uint32_t tot_clocks;
		uint8_t counter;
		uint8_t stop_on_expire;
	} length;

	struct
	{
		uint8_t counter;
		uint32_t cur_clocks;
		uint32_t tot_clocks;
		enum envelope_dir direction;
	} envelope;

	struct
	{
		int32_t left;
		int32_t right;
	} sample;

	uint8_t channel;
	uint8_t is_disabled;
};

void square_init(struct square *s, uint8_t channel);
void square_run(struct square *s, blip_t *blip_left, blip_t *blip_right, uint32_t time_inc);
void square_sweep_shadow(struct square *s);

struct wave
{
	struct
	{
		uint32_t cur_clocks;
		uint32_t tot_clocks;
	} period;

	struct
	{
		uint32_t cur_clocks;
		uint32_t tot_clocks;
		uint16_t counter;
		uint8_t stop_on_expire;
	} length;

	struct
	{
		/* 4 bit samples buffer
		 */
		uint8_t *buffer;
		/* current sample position
		 */
		uint8_t pos;
		/* 4 bit samples count
		 */
		uint8_t count;
	} wave;

	struct
	{
		int32_t left;
		int32_t right;
	} sample;

	uint8_t channel;
	uint8_t is_disabled;
};

void wave_init(struct wave *w, uint8_t *samples_buffer, uint8_t samples_count, uint8_t channel);
void wave_run(struct wave *w, blip_t *blip_left, blip_t *blip_right, uint32_t clocks);

struct noise
{
	struct
	{
		uint32_t cur_clocks;
		uint32_t tot_clocks;
	} period;

	struct
	{
		uint32_t cur_clocks;
		uint32_t tot_clocks;
		uint16_t counter;
		uint8_t stop_on_expire;
	} length;

	struct
	{
		uint8_t counter;
		uint32_t cur_clocks;
		uint32_t tot_clocks;
		enum envelope_dir direction;
	} envelope;

	struct
	{
		uint32_t counter;
		enum { LFSR7, LFSR15 } width;
	} lfsr;

	struct
	{
		int32_t left;
		int32_t right;
	} sample;

	uint8_t channel;
	uint8_t is_disabled;
};

void noise_init(struct noise *w, uint8_t channel);
void noise_run(struct noise *w, blip_t *blip_left, blip_t *blip_right, uint32_t clocks);


#endif
