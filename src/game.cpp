#include "game.h"
#include "types.h"
#include <math.h>
#include <stdio.h>

struct GameState {
	int x_offset, y_offset;
};

void game_output_sound(GameSoundBuffer& sound_output, const int tone_hz)
{
	static float t_sine = 0;
	const auto tone_volume = 3000;
	const auto wave_period = (float)sound_output.frame_rate / tone_hz;
	for (int i = 0; i < sound_output.frame_count; i++) {
		auto sample_index = i * sound_output.channel_num;
		auto sine_value = sinf(t_sine);
		i16 value = sine_value * tone_volume;
		sound_output.sample_buffer[sample_index] = value;
		sound_output.sample_buffer[sample_index + 1] = value;
		t_sine += M_PIf32 * 2.f / wave_period;
	}
}

void game_draw_thing(const GameScreenBuffer& buffer, const int x_offset, const int y_offset)
{
	for (int y = 0; y < buffer.height; y++) {
		auto row = buffer.buffer + (y * buffer.pitch());
		for (int x = 0; x < buffer.width; x++) {
			auto p = (u32*)(row + (x * buffer.pixel_bytes()));

			*p = (u8)(y + y_offset) | (u8)(x + x_offset);
		}
	}
}

void game_update_and_render(GameMemory& mem, const GameScreenBuffer& buffer, GameSoundBuffer& sound_buffer, const GameInput& input)
{
	assert(sizeof(GameState) <= mem.perm_storage_size);
	auto& state = *(GameState*)mem.perm_storage;
	if (!mem.is_initialized) {
		state.x_offset = 0;
		state.y_offset = 0;
		mem.is_initialized = true;

		const auto file = platform_read_entire_file(__FILE__);
		if (file.mem) {
			platform_write_entire_file("arroz.txt", file.mem, file.size);
			free(file.mem);
		}
	}

	const auto& input0 = input.ctrls[0];
	const auto& input1 = input.ctrls[1];
	const auto tone_hz = 256 + (int)(128.f * input1.end_x);
	state.x_offset += (int)(4.0f * input1.end_x);
	state.y_offset += (int)(4.0f * input1.end_y);

	// printf("%i\n", input0.down.ended_down);
	if (input0.down.ended_down) {
		state.y_offset += 1;
	}
	if (input0.up.ended_down) {
		state.y_offset -= 1;
	}
	if (input0.left.ended_down) {
		state.x_offset -= 1;
	}
	if (input0.right.ended_down) {
		state.x_offset += 1;
	}

	game_output_sound(sound_buffer, tone_hz);
	game_draw_thing(buffer, state.x_offset, state.y_offset);
}
