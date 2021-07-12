#pragma once
#include "types.h"
#include <alsa/asoundlib.h>

struct SoundOutput {
	const int bit_depth = 16; // @Volatile_bit_depth
	const int frame_rate = 48000;
	const int channel_num = 2;
	const int length = 1;
	i16* sample_buffer; // @Volatile_bit_depth
	snd_pcm_t* handle;

	float hz;
	const int tone_volume = 3000;
	float t_sine;

	float wave_period() const { return (float)frame_rate / hz; };
	int frame_count() const { return frame_rate * length; };
	int bytes_per_frame() const { return (bit_depth / 8) * channel_num; };
	int byte_size() const { return frame_count() * bytes_per_frame(); };
};
