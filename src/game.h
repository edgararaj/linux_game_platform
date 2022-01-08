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

struct GameBtnState {
	int half_trans_count;
	bool ended_down;
};

struct GameCtrlInput {
	bool is_analog;

	float start_x;
	float start_y;
	float min_x;
	float min_y;
	float max_x;
	float max_y;
	float end_x;
	float end_y;

	union {
		GameBtnState buttons[6];
		struct {
			GameBtnState up;
			GameBtnState down;
			GameBtnState left;
			GameBtnState right;
			GameBtnState lb;
			GameBtnState rb;
		};
	};
};

struct GameInput {
	GameCtrlInput ctrls[4]; // @Volatile_max_joy_count
};

struct GameMemory {
	u64 perm_storage_size;
	void* perm_storage;
	u64 trans_storage_size;
	void* trans_storage;
	bool is_initialized;
};

void game_update_and_render(GameMemory& memory, const GameScreenBuffer& buffer, GameSoundBuffer& sound_buffer, const GameInput& input);
