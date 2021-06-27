#pragma once
#include "types.h"
#include <alsa/asoundlib.h>

struct SoundBuffer {
	const int bit_depth = 16; // @Volatile_bit_depth
	int frame_rate;
	int channel_num;
	int length;
	i16* sample_buffer; // @Volatile_bit_depth
	snd_pcm_t* handle;

	int frame_count() const { return frame_rate * length; };
	int bytes_per_frame() const { return (bit_depth / 8) * channel_num; };
	int byte_size() const { return frame_count() * bytes_per_frame(); };
};
