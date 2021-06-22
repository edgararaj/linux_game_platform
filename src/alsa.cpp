#include <alsa/asoundlib.h>
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

bool init_alsa()
{
	auto dl_name = "libasound.so";
	auto alsa_dl = dlopen(dl_name, RTLD_LAZY);
	if (!alsa_dl) {
		printf("[ALSA] Couldn't load %s\n", dl_name);
		return false;
	}

	int error;
	snd_output_t* log;
	auto dy_snd_output_stdio_attach = (snd_output_stdio_attach_fun*)dlsym(alsa_dl, "snd_output_stdio_attach");
	if (dy_snd_output_stdio_attach) {
		error = dy_snd_output_stdio_attach(&log, stderr, 0);
		if (error < 0) {
			printf("[ALSA] snd_output_stdio_attach failed\n");
			return false;
		}
	}

	snd_pcm_t* handle;
	auto dy_snd_pcm_open = (snd_pcm_open_fun*)dlsym(alsa_dl, "snd_pcm_open");
	if (dy_snd_output_stdio_attach) {
		error = dy_snd_pcm_open(&handle, "default", SND_PCM_STREAM_PLAYBACK, 0);
		if (error < 0) {
			printf("[ALSA] snd_pcm_open failed\n");
			return false;
		}
	}

	snd_pcm_hw_params_t* hw_params;
	auto dy_snd_pcm_hw_params_sizeof_fun = (snd_pcm_hw_params_sizeof_fun*)dlsym(alsa_dl, "snd_pcm_hw_params_sizeof");
	if (dy_snd_pcm_hw_params_sizeof_fun) {
#define snd_pcm_hw_params_sizeof dy_snd_pcm_hw_params_sizeof_fun
		snd_pcm_hw_params_alloca(&hw_params);
#undef snd_pcm_hw_params_sizeof
	}

	auto dy_snd_pcm_hw_params_any = (snd_pcm_hw_params_any_fun*)dlsym(alsa_dl, "snd_pcm_hw_params_any");
	if (dy_snd_pcm_hw_params_any) {
		error = dy_snd_pcm_hw_params_any(handle, hw_params);
		if (error < 0) {
			printf("[ALSA] snd_pcm_hw_params_any failed\n");
			return false;
		}
	}

	auto dy_snd_pcm_hw_params_set_access = (snd_pcm_hw_params_set_access_fun*)dlsym(alsa_dl, "snd_pcm_hw_params_set_access");
	if (dy_snd_pcm_hw_params_set_access) {
		error = dy_snd_pcm_hw_params_set_access(handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
		if (error < 0) {
			printf("[ALSA] snd_pcm_hw_params_set_access failed\n");
			return false;
		}
	}

	auto dy_snd_pcm_hw_params_set_format = (snd_pcm_hw_params_set_format_fun*)dlsym(alsa_dl, "snd_pcm_hw_params_set_format");
	if (dy_snd_pcm_hw_params_set_format) {
		error = dy_snd_pcm_hw_params_set_format(handle, hw_params, SND_PCM_FORMAT_S16_LE);
		if (error < 0) {
			printf("[ALSA] snd_pcm_hw_params_set_format failed\n");
			return false;
		}
	}

	auto dy_snd_pcm_hw_params_set_channels = (snd_pcm_hw_params_set_channels_fun*)dlsym(alsa_dl, "snd_pcm_hw_params_set_channels");
	if (dy_snd_pcm_hw_params_set_channels) {
		error = dy_snd_pcm_hw_params_set_channels(handle, hw_params, 2);
		if (error < 0) {
			printf("[ALSA] snd_pcm_hw_params_set_channels failed\n");
			return false;
		}
	}

	auto dy_snd_pcm_hw_params_set_rate = (snd_pcm_hw_params_set_rate_fun*)dlsym(alsa_dl, "snd_pcm_hw_params_set_rate");
	if (dy_snd_pcm_hw_params_set_rate) {
		error = dy_snd_pcm_hw_params_set_rate(handle, hw_params, 48000, 0);
		if (error < 0) {
			printf("[ALSA] snd_pcm_hw_params_set_rate failed\n");
			return false;
		}
	}

	auto dy_snd_pcm_hw_params_set_buffer_size = (snd_pcm_hw_params_set_buffer_size_fun*)dlsym(alsa_dl, "snd_pcm_hw_params_set_buffer_size");
	if (dy_snd_pcm_hw_params_set_buffer_size) {
		error = dy_snd_pcm_hw_params_set_buffer_size(handle, hw_params, 48000);
		if (error < 0) {
			printf("[ALSA] snd_pcm_hw_params_set_buffer_size failed\n");
			return false;
		}
	}

	auto dy_snd_pcm_hw_params = (snd_pcm_hw_params_fun*)dlsym(alsa_dl, "snd_pcm_hw_params");
	if (dy_snd_pcm_hw_params) {
		error = dy_snd_pcm_hw_params(handle, hw_params);
		if (error < 0) {
			printf("[ALSA] snd_pcm_hw_params failed\n");
			return false;
		}
	}

	snd_pcm_uframes_t period;
	auto dy_snd_pcm_hw_params_get_period_size = (snd_pcm_hw_params_get_period_size_fun*)dlsym(alsa_dl, "snd_pcm_hw_params_get_period_size");
	if (dy_snd_pcm_hw_params_get_period_size) {
		error = dy_snd_pcm_hw_params_get_period_size(hw_params, &period, 0);
		if (error < 0) {
			printf("[ALSA] snd_pcm_hw_params_get_period_size failed\n");
			return false;
		}
	}

	auto dy_snd_pcm_dump = (snd_pcm_dump_fun*)dlsym(alsa_dl, "snd_pcm_dump");
	if (dy_snd_pcm_dump) {
		error = dy_snd_pcm_dump(handle, log);
		if (error < 0) {
			printf("[ALSA] snd_pcm_dump failed\n");
			return false;
		}
	}

	return true;
}
