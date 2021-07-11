#pragma once
#include "types.h"
#include <alsa/asoundlib.h>

struct SoundOutput {
	const uint bit_depth = 16; // @Volatile_bit_depth
	const uint frame_rate = 48000;
	const uint channel_num = 2;
	const uint length = 1;
	i16* sample_buffer; // @Volatile_bit_depth
	snd_pcm_t* handle;

	uint hz = 256;
	const uint tone_volume = 3000;
	float t_sine = 0;

	uint wave_period() const { return frame_rate / hz; };
	uint frame_count() const { return frame_rate * length; };
	uint bytes_per_frame() const { return (bit_depth / 8) * channel_num; };
	uint byte_size() const { return frame_count() * bytes_per_frame(); };
};
