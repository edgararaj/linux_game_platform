#pragma once
#include "types.h"

struct GameScreenBuffer {
	int width;
	int height;
	int pixel_bits;
	char* buffer;
	int pixel_bytes() const
	{
		return pixel_bits / 8;
	}
	int pitch() const { return width * pixel_bytes(); }
};

struct GameSoundBuffer {
	const int frame_rate;
	const int channel_num;
	i16* const sample_buffer; // @Volatile_bit_depth
	const long frame_count;
};
