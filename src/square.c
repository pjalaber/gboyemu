#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "square.h"
#include "sound.h"
#include "lfsr.h"

#define VOLUME_DIVIDER 4
#define SIGNAL0 (INT16_MIN / VOLUME_DIVIDER)
#define SIGNAL1 (INT16_MAX / VOLUME_DIVIDER)

static const int32_t waveform_data[4][8] =
{
	{ SIGNAL0, SIGNAL0, SIGNAL0, SIGNAL0, SIGNAL0, SIGNAL0, SIGNAL0, SIGNAL1},
	{ SIGNAL1, SIGNAL0, SIGNAL0, SIGNAL0, SIGNAL0, SIGNAL0, SIGNAL0, SIGNAL1},
	{ SIGNAL1, SIGNAL0, SIGNAL0, SIGNAL0, SIGNAL0, SIGNAL1, SIGNAL1, SIGNAL1},
	{ SIGNAL0, SIGNAL1, SIGNAL1, SIGNAL1, SIGNAL1, SIGNAL1, SIGNAL1, SIGNAL0},
};

void square_init(struct square *s, uint8_t channel)
{
	assert(channel >= 1 && channel <= 4);

	s->period.cur_clocks = 0;
	s->period.tot_clocks = 0;
	s->period.counter = 0;
	s->period.waveform = 2;

	s->sweep.shadow_freq = 0;
	s->sweep.cur_clocks = 0;
	s->sweep.tot_clocks = 0;
	s->sweep.direction = SWEEP_INC;

	s->length.cur_clocks = 0;
	s->length.tot_clocks = 0;
	s->length.counter = 0;
	s->length.stop_on_expire = 0;

	s->envelope.counter = 0;
	s->envelope.cur_clocks = 0;
	s->envelope.tot_clocks = 0;
	s->envelope.direction = ENVELOPE_INC;

	s->sample.left = 0;
	s->sample.right = 0;

	s->is_disabled = 0;
	s->channel = channel;
}

static inline void square_add_delta(blip_t *blip, uint32_t clocks,
	int32_t *cur_sample, int32_t new_sample)
{
	if ( new_sample != *cur_sample )
	{
		blip_add_delta(blip, clocks, new_sample - *cur_sample);
		*cur_sample = new_sample;
	}
}

static inline void square_output_sample(uint32_t channel, blip_t *blip_left, blip_t *blip_right,
	uint32_t clocks, int32_t sample, int32_t *sample_left, int32_t *sample_right)
{
	if ( sound_channel_left_is_on(channel) )
	{
		square_add_delta(blip_left, clocks, sample_left,
			sound_adjust_left_sample_volume(sample));
	}
	if ( sound_channel_right_is_on(channel) )
	{
		square_add_delta(blip_right, clocks, sample_right,
			sound_adjust_right_sample_volume(sample));
	}
}

void square_run(struct square *s, blip_t *blip_left, blip_t *blip_right, uint32_t clocks)
{
	uint32_t c;
	int32_t sample;

	if ( s->is_disabled )
		return;

	for ( c = 0; c < clocks; c++ )
	{
		/* sweep
		 */
		if ( s->sweep.tot_clocks > 0 )
		{
			if ( s->sweep.cur_clocks >= s->sweep.tot_clocks )
			{
				s->sweep.cur_clocks -= s->sweep.tot_clocks;
				sound_sweep_shadow(s);
				if ( s->is_disabled )
				{
					sound_channel_set_readonly_register_status(s->channel, 0);
					break;
				}
			}
			s->sweep.cur_clocks++;
		}

		/* period
		 */
		if ( s->period.cur_clocks >= s->period.tot_clocks )
		{
			s->period.cur_clocks -= s->period.tot_clocks;
			sample = waveform_data[s->period.waveform][s->period.counter];
			s->period.counter = (s->period.counter + 1) % 8;
			sample = (sample / 15) * s->envelope.counter;
			square_output_sample(s->channel, blip_left, blip_right, c,
				sample, &s->sample.left, &s->sample.right);
		}
		s->period.cur_clocks++;

		/* length
		 */
		if ( s->length.stop_on_expire )
		{
			if ( s->length.cur_clocks >= s->length.tot_clocks )
			{
				s->length.cur_clocks -= s->length.tot_clocks;
				if ( s->length.counter > 0 )
				{
					s->length.counter--;
					if ( s->length.stop_on_expire && s->length.counter == 0 )
					{
						/* channel is no more active: exit
						 */
						s->is_disabled = 1;
						sound_channel_set_readonly_register_status(s->channel, 0);
						break;
					}
				}
			}

			s->length.cur_clocks++;
		}

		/* envelope
		 */
		if ( s->envelope.tot_clocks > 0 )
		{
			if ( s->envelope.cur_clocks >= s->envelope.tot_clocks )
			{
				assert(s->envelope.counter <= 15);
				s->envelope.cur_clocks -= s->envelope.tot_clocks;
				if ( s->envelope.direction == ENVELOPE_DEC && s->envelope.counter > 0 )
				{
					s->envelope.counter--;
				}
				else if ( s->envelope.direction == ENVELOPE_INC && s->envelope.counter < 15 )
				{
					s->envelope.counter++;
				}
			}
			s->envelope.cur_clocks++;
		}
	}
}

void square_sweep_shadow(struct square *s)
{
	uint32_t last_freq;

	if ( s->sweep.direction == SWEEP_INC )
	{
		s->sweep.shadow_freq += (s->sweep.shadow_freq >> s->sweep.shift);
		if ( s->sweep.shadow_freq > 2047 )
		{
			s->sweep.shadow_freq = 2048;
			s->is_disabled = 1;
		}
	}
	else
	{
		last_freq = s->sweep.shadow_freq;
		s->sweep.shadow_freq -= (s->sweep.shadow_freq >> s->sweep.shift);
		if ( s->sweep.shadow_freq > 2047 )
		{
			s->sweep.shadow_freq = last_freq;
			s->is_disabled = 1;
		}
	}
}

void wave_init(struct wave *w, uint8_t *samples_buffer, uint8_t samples_count, uint8_t channel)
{
	w->period.cur_clocks = 0;
	w->period.tot_clocks = 0;

	w->length.cur_clocks = 0;
	w->length.counter = 0;
	w->length.stop_on_expire = 0;

	w->wave.buffer = samples_buffer;
	w->wave.count = samples_count;
	w->wave.pos = 0;

	w->sample.left = 0;
	w->sample.right = 0;

	w->channel = channel;
	w->is_disabled = 0;
}

void wave_run(struct wave *w, blip_t *blip_left, blip_t *blip_right, uint32_t clocks)
{
	uint32_t c;
	int32_t sample;
	uint8_t wave_sample;

	if ( w->is_disabled )
		return;

	for ( c = 0; c < clocks; c++ )
	{
		/* period
		 */
		if ( w->period.cur_clocks >= w->period.tot_clocks )
		{
			w->period.cur_clocks -= w->period.tot_clocks;
			if ( (w->wave.pos % 2) == 0 )
				wave_sample = (w->wave.buffer[w->wave.pos / 2] >> 4) & 0xF;
			else
				wave_sample = w->wave.buffer[w->wave.pos / 2] & 0xF;
			w->wave.pos = (w->wave.pos + 1) % w->wave.count;

			wave_sample = sound_adjust_wave_sample_volume(wave_sample);
			sample = (wave_sample - 7) * 4096 / VOLUME_DIVIDER;
			square_output_sample(w->channel, blip_left, blip_right, c,
				sample, &w->sample.left, &w->sample.right);
		}
		w->period.cur_clocks++;

		/* length
		 */
		if ( w->length.stop_on_expire )
		{
			if ( w->length.cur_clocks >= w->length.tot_clocks )
			{
				w->length.cur_clocks -= w->length.tot_clocks;
				if ( w->length.counter > 0 )
				{
					w->length.counter--;
					if ( w->length.stop_on_expire && w->length.counter == 0 )
					{
						/* channel is no more active: exit
						 */
						w->is_disabled = 1;
						sound_channel_set_readonly_register_status(w->channel, 0);
						break;
					}
				}
			}

			w->length.cur_clocks++;
		}
	}
}


void noise_init(struct noise *n, uint8_t channel)
{
	n->period.cur_clocks = 0;
	n->period.tot_clocks = 0;

	n->length.cur_clocks = 0;
	n->length.counter = 0;
	n->length.stop_on_expire = 0;

	n->envelope.counter = 0;
	n->envelope.cur_clocks = 0;
	n->envelope.tot_clocks = 0;
	n->envelope.direction = ENVELOPE_INC;

	n->lfsr.width = LFSR7;
	n->lfsr.counter = 0;

	n->sample.left = 0;
	n->sample.right = 0;

	n->channel = channel;
	n->is_disabled = 0;
}

void noise_run(struct noise *n, blip_t *blip_left, blip_t *blip_right, uint32_t clocks)
{
	uint32_t c;
	uint8_t lfsr_bit;
	int32_t sample;

	if ( n->is_disabled )
		return;

	for ( c = 0; c < clocks; c++ )
	{
		/* period
		 */
		if ( n->period.cur_clocks >= n->period.tot_clocks )
		{
			n->period.cur_clocks -= n->period.tot_clocks;
			switch ( n->lfsr.width )
			{
				case LFSR7:
				lfsr_bit = lfsr7_table[n->lfsr.counter];
				n->lfsr.counter++;
				if ( n->lfsr.counter >= 127 )
					n->lfsr.counter = 0;
				break;

				case LFSR15:
				lfsr_bit = lfsr15_table[n->lfsr.counter];
				n->lfsr.counter++;
				if ( n->lfsr.counter >= 32767 )
					n->lfsr.counter = 0;
				break;
			}

			sample = (lfsr_bit) ? SIGNAL1 : SIGNAL0;
			sample = (sample / 15) * n->envelope.counter;
			square_output_sample(n->channel, blip_left, blip_right, c,
				sample, &n->sample.left, &n->sample.right);
		}
		n->period.cur_clocks++;

		/* length
		 */
		if ( n->length.stop_on_expire )
		{
			if ( n->length.cur_clocks >= n->length.tot_clocks )
			{
				n->length.cur_clocks -= n->length.tot_clocks;
				if ( n->length.counter > 0 )
				{
					n->length.counter--;
					if ( n->length.stop_on_expire && n->length.counter == 0 )
					{
						/* channel is no more active: exit
						 */
						n->is_disabled = 1;
						sound_channel_set_readonly_register_status(n->channel, 0);
						break;
					}
				}
			}

			n->length.cur_clocks++;
		}

		/* envelope
		 */
		if ( n->envelope.tot_clocks > 0 )
		{
			if ( n->envelope.cur_clocks >= n->envelope.tot_clocks )
			{
				assert(n->envelope.counter <= 15);
				n->envelope.cur_clocks -= n->envelope.tot_clocks;
				if ( n->envelope.direction == ENVELOPE_DEC && n->envelope.counter > 0 )
				{
					n->envelope.counter--;
				}
				else if ( n->envelope.direction == ENVELOPE_INC && n->envelope.counter < 15 )
				{
					n->envelope.counter++;
				}
			}
			n->envelope.cur_clocks++;
		}
	}
}
