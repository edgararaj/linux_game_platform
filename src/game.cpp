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

void game_update_and_render(GameMemory& memory, const GameScreenBuffer& buffer, GameSoundBuffer& sound_buffer, const GameInput& input)
{
	assert(sizeof(GameState) <= memory.perm_storage_size);
	auto& state = *(GameState*)memory.perm_storage;
	if (!memory.is_initialized) {
		state.x_offset = 0;
		state.y_offset = 0;
		memory.is_initialized = true;
	}

	const auto& input0 = input.ctrls[0];
	const auto tone_hz = 256 + (int)(128.f * input0.end_x);
	state.x_offset += (int)(4.0f * input0.end_x);
	state.y_offset += (int)(4.0f * input0.end_y);

	if (input0.down.ended_down) {
		state.x_offset += 1;
	}

	game_output_sound(sound_buffer, tone_hz);
	game_draw_thing(buffer, state.x_offset, state.y_offset);
}
