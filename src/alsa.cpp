#include "x11_platform.h"
#include <dlfcn.h>
#include <fcntl.h>

#define SND_OUTPUT_STDIO_ATTACH(name) \
	int name(snd_output_t** outputp, FILE* fp, int _close)
typedef SND_OUTPUT_STDIO_ATTACH(snd_output_stdio_attach_fun);

#define SND_PCM_OPEN(name) \
	int name(snd_pcm_t** pcm, const char* name, snd_pcm_stream_t stream, int mode)
typedef SND_PCM_OPEN(snd_pcm_open_fun);

#define SND_PCM_HW_PARAMS_SIZEOF(name) \
	size_t name(void)
typedef SND_PCM_HW_PARAMS_SIZEOF(snd_pcm_hw_params_sizeof_fun);

#define SND_PCM_HW_PARAMS_ANY(name) \
	int name(snd_pcm_t* pcm, snd_pcm_hw_params_t* params)
typedef SND_PCM_HW_PARAMS_ANY(snd_pcm_hw_params_any_fun);

#define SND_PCM_HW_PARAMS_SET_ACCESS(name) \
	int name(snd_pcm_t* pcm, snd_pcm_hw_params_t* params, snd_pcm_access_t _access)
typedef SND_PCM_HW_PARAMS_SET_ACCESS(snd_pcm_hw_params_set_access_fun);

#define SND_PCM_HW_PARAMS_SET_FORMAT(name) \
	int name(snd_pcm_t* pcm, snd_pcm_hw_params_t* params, snd_pcm_format_t val)
typedef SND_PCM_HW_PARAMS_SET_FORMAT(snd_pcm_hw_params_set_format_fun);

#define SND_PCM_HW_PARAMS_SET_CHANNELS(name) \
	int name(snd_pcm_t* pcm, snd_pcm_hw_params_t* params, unsigned int val)
typedef SND_PCM_HW_PARAMS_SET_CHANNELS(snd_pcm_hw_params_set_channels_fun);

#define SND_PCM_HW_PARAMS_SET_RATE(name) \
	int name(snd_pcm_t* pcm, snd_pcm_hw_params_t* params, unsigned int val, int dir)
typedef SND_PCM_HW_PARAMS_SET_RATE(snd_pcm_hw_params_set_rate_fun);

#define SND_PCM_HW_PARAMS_SET_BUFFER_SIZE(name) \
	int name(snd_pcm_t* pcm, snd_pcm_hw_params_t* params, snd_pcm_uframes_t val)
typedef SND_PCM_HW_PARAMS_SET_BUFFER_SIZE(snd_pcm_hw_params_set_buffer_size_fun);

#define SND_PCM_HW_PARAMS(name) \
	int name(snd_pcm_t* pcm, snd_pcm_hw_params_t* params)
typedef SND_PCM_HW_PARAMS(snd_pcm_hw_params_fun);

#define SND_PCM_HW_PARAMS_GET_PERIOD_SIZE(name) \
	int name(const snd_pcm_hw_params_t* params, snd_pcm_uframes_t* frames, int* dir)
typedef SND_PCM_HW_PARAMS_GET_PERIOD_SIZE(snd_pcm_hw_params_get_period_size_fun);

#define SND_PCM_DUMP(name) \
	int name(snd_pcm_t* pcm, snd_output_t* out)
typedef SND_PCM_DUMP(snd_pcm_dump_fun);

#define SND_PCM_WRITEI(name) \
	snd_pcm_sframes_t name(snd_pcm_t* pcm, const void* buffer, snd_pcm_uframes_t size);
typedef SND_PCM_WRITEI(snd_pcm_writei_fun);

#define SND_PCM_RECOVER(name) \
	int name(snd_pcm_t* pcm, int err, int silent);
typedef SND_PCM_RECOVER(snd_pcm_recover_fun);

#define SND_PCM_PREPARE(name) \
	int name(snd_pcm_t* pcm);
typedef SND_PCM_PREPARE(snd_pcm_prepare_fun);

#define SND_PCM_AVAIL_DELAY(name) \
	int name(snd_pcm_t* pcm, snd_pcm_sframes_t* availp, snd_pcm_sframes_t* delayp);
typedef SND_PCM_AVAIL_DELAY(snd_pcm_avail_delay_fun);

bool init_alsa(SoundBuffer& sound_buffer, void* alsa_dl, int frame_rate, int channel_num, int buffer_length)
{
	sound_buffer.frame_rate = frame_rate;
	sound_buffer.channel_num = channel_num;
	sound_buffer.length = buffer_length;

	int error;

#define ALSA_CALL(name, ...)                             \
	auto dy_##name = (name##_fun*)dlsym(alsa_dl, #name); \
	if (!dy_##name)                                      \
		return false;                                    \
	error = dy_##name(__VA_ARGS__);                      \
	if (error < 0) {                                     \
		printf("[ALSA]: " #name "failed\n");             \
		return false;                                    \
	}

	snd_output_t* log;
	ALSA_CALL(snd_output_stdio_attach, &log, stderr, 0);

	ALSA_CALL(snd_pcm_open, &sound_buffer.handle, "default", SND_PCM_STREAM_PLAYBACK, 0)

	snd_pcm_hw_params_t* hw_params;
	auto dy_snd_pcm_hw_params_sizeof = (snd_pcm_hw_params_sizeof_fun*)dlsym(alsa_dl, "snd_pcm_hw_params_sizeof");
	if (!dy_snd_pcm_hw_params_sizeof)
		return false;
#define snd_pcm_hw_params_sizeof dy_snd_pcm_hw_params_sizeof
	snd_pcm_hw_params_alloca(&hw_params);
#undef snd_pcm_hw_params_sizeof

	ALSA_CALL(snd_pcm_hw_params_any, sound_buffer.handle, hw_params);
	ALSA_CALL(snd_pcm_hw_params_set_access, sound_buffer.handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
	ALSA_CALL(snd_pcm_hw_params_set_format, sound_buffer.handle, hw_params, SND_PCM_FORMAT_S16_LE); // @Volatile_bit_depth
	ALSA_CALL(snd_pcm_hw_params_set_channels, sound_buffer.handle, hw_params, sound_buffer.channel_num);
	ALSA_CALL(snd_pcm_hw_params_set_rate, sound_buffer.handle, hw_params, sound_buffer.frame_rate, 0);
	ALSA_CALL(snd_pcm_hw_params_set_buffer_size, sound_buffer.handle, hw_params, sound_buffer.frame_count());
	ALSA_CALL(snd_pcm_hw_params, sound_buffer.handle, hw_params);
	//ALSA_CALL(snd_pcm_prepare, sound_buffer.handle);

	//snd_pcm_uframes_t period;
	//ALSA_CALL(snd_pcm_hw_params_get_period_size, hw_params, &period, 0);
	ALSA_CALL(snd_pcm_dump, sound_buffer.handle, log);

#undef ALSA_CALL

	return true;
}
