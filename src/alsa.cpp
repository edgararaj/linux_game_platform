#include "x11_platform.h"
#include <alsa/asoundlib.h>

struct SoundOutput {
	const int bit_depth = 16; // @Volatile_bit_depth
	const int frame_rate = 48000;
	const int channel_num = 2;
	const int length = 1;
	i16* sample_buffer; // @Volatile_bit_depth
	snd_pcm_t* handle;

	int frame_count() const { return frame_rate * length; };
	int bytes_per_frame() const { return (bit_depth / 8) * channel_num; };
	int byte_size() const { return frame_count() * bytes_per_frame(); };
};

bool alsa_setup(SoundOutput& sound_buffer)
{
	int error;

#define ALSA_CALL(name, ...)                 \
	if (name(__VA_ARGS__) < 0) {             \
		printf("[ALSA]: " #name "failed\n"); \
		return false;                        \
	}

	snd_output_t* log;
	ALSA_CALL(snd_output_stdio_attach, &log, stderr, 0);

	ALSA_CALL(snd_pcm_open, &sound_buffer.handle, "default", SND_PCM_STREAM_PLAYBACK, 0)

	snd_pcm_hw_params_t* hw_params;
	snd_pcm_hw_params_alloca(&hw_params);

	ALSA_CALL(snd_pcm_hw_params_any, sound_buffer.handle, hw_params);
	ALSA_CALL(snd_pcm_hw_params_set_access, sound_buffer.handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
	ALSA_CALL(snd_pcm_hw_params_set_format, sound_buffer.handle, hw_params, SND_PCM_FORMAT_S16_LE); // @Volatile_bit_depth
	ALSA_CALL(snd_pcm_hw_params_set_channels, sound_buffer.handle, hw_params, sound_buffer.channel_num);
	ALSA_CALL(snd_pcm_hw_params_set_rate, sound_buffer.handle, hw_params, sound_buffer.frame_rate, 0);
	ALSA_CALL(snd_pcm_hw_params_set_buffer_size, sound_buffer.handle, hw_params, sound_buffer.frame_count());
	ALSA_CALL(snd_pcm_hw_params, sound_buffer.handle, hw_params);
	// ALSA_CALL(snd_pcm_prepare, sound_buffer.handle);

	// snd_pcm_uframes_t period;
	// ALSA_CALL(snd_pcm_hw_params_get_period_size, hw_params, &period, 0);
	ALSA_CALL(snd_pcm_dump, sound_buffer.handle, log);

#undef ALSA_CALL

	return true;
}
