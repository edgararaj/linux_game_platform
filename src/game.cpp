#include "game.h"
#include "types.h"

void game_update_and_render(const GameScreenBuffer& buffer, const int x_offset, const int y_offset)
{
	for (int y = 0; y < buffer.height; y++) {
		auto row = buffer.buffer + (y * buffer.pitch());
		for (int x = 0; x < buffer.width; x++) {
			auto p = (u32*)(row + (x * buffer.pixel_bytes()));

			*p = (u8)(y + y_offset) << 8 | (u8)(x + x_offset);
		}
	}
}
