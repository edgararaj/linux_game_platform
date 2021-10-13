#pragma once

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
