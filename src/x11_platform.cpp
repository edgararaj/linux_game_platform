#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/joystick.h>
#include <malloc.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "alsa.cpp"
#include "optional.h"
#include "types.h"
#include "x11_platform.h"

#define MAX_EVENTS 1024
#define LEN_NAME 16
#define EVENT_SIZE sizeof(inotify_event)
#define BUF_LEN (MAX_EVENTS * (EVENT_SIZE + LEN_NAME))

#define ALSA_DEBUG 1
#define FPS 0

auto use_xshm = true;

int get_time_in_ns()
{
	timespec spec;
	clock_gettime(CLOCK_MONOTONIC, &spec);

	return spec.tv_nsec;
}

snd_pcm_writei_fun* dy_snd_pcm_writei;
snd_pcm_recover_fun* dy_snd_pcm_recover;
snd_pcm_avail_delay_fun* dy_snd_pcm_avail_delay;
snd_pcm_drain_fun* dy_snd_pcm_drain;
snd_pcm_close_fun* dy_snd_pcm_close;

struct ScreenBuffer {
	int width;
	int height;
	int pixel_bits;
	char* buffer;
	XImage* ximage;
	XShmSegmentInfo shminfo;
	int pixel_bytes() const
	{
		return pixel_bits / 8;
	}
	int byte_size() const { return width * height * pixel_bytes(); }
	int pitch() const { return width * pixel_bytes(); }
};

void delete_screen_buffer(ScreenBuffer& buffer, Display* display)
{
	if (use_xshm) {
		XShmDetach(display, &buffer.shminfo);
		shmdt(buffer.shminfo.shmaddr);
	}
	XDestroyImage(buffer.ximage);
}

bool create_screen_buffer(ScreenBuffer& buffer, int width, int height, int pixel_bits, const XVisualInfo& vinfo, Display* display)
{
	buffer.width = width;
	buffer.height = height;
	buffer.pixel_bits = pixel_bits;

	if (use_xshm) {
		// TODO: Cant create a new shared buffer when resized, it crashes the app, maybe allocate a large one and use the portion needed
		buffer.ximage = XShmCreateImage(display, vinfo.visual, vinfo.depth, ZPixmap, 0, &buffer.shminfo, width, height);
		buffer.shminfo.shmid = shmget(IPC_PRIVATE, buffer.ximage->bytes_per_line * buffer.ximage->height, IPC_CREAT | 0777);
		buffer.buffer = buffer.shminfo.shmaddr = buffer.ximage->data = (char*)shmat(buffer.shminfo.shmid, 0, 0);
		buffer.shminfo.readOnly = 1;

		if (!XShmAttach(display, &buffer.shminfo)) {
			fprintf(stderr, "Failed to XShmAttach\n");
			return 0;
		}

		shmctl(buffer.shminfo.shmid, IPC_RMID, 0); // Mark shared buffer for removal after process end; cant do on create_buffer because it crashes

		printf("[MIT-SHM]: Shared memory KID=%d, at=%p\n", buffer.shminfo.shmid, buffer.shminfo.shmaddr);

	} else {
		buffer.buffer = (char*)malloc(buffer.byte_size());
		if (!buffer.buffer) {
			fprintf(stderr, "Failed to malloc\n");
			return 0;
		}

		buffer.ximage = XCreateImage(display, vinfo.visual, vinfo.depth, ZPixmap, 0, buffer.buffer, width, height, pixel_bits, 0);
	}

	return 1;
}

struct Joystick {
	int range_min;
	int range_max;
	int fd;
	float axis0;
	float axis1;
};

bool get_joystick(Joystick& joy, const char* const path)
{
	bool result = false;
	joy.fd = open(path, O_RDONLY | O_NONBLOCK);
	if (joy.fd < 0) {
		fprintf(stderr, "Couldn't open %s\n", path);
		return result;
	}

	u8 num_axis = 0;
	u8 num_buttons = 0;
	ioctl(joy.fd, JSIOCGAXES, &num_axis);
	ioctl(joy.fd, JSIOCGBUTTONS, &num_buttons);

	char name[1024];
	ioctl(joy.fd, JSIOCGNAME(sizeof(name)), name);

	printf("[JOYSTICK]: %s is connected (Axis: %i, Buttons: %i)\n", name, num_axis, num_buttons);

	auto corr = (js_corr*)malloc(num_axis * sizeof(js_corr));
	ioctl(joy.fd, JSIOCGCORR, corr);

	if (corr && corr->type) {
		auto center_min = corr->coef[0];
		auto center_max = corr->coef[1];

		auto invert = (corr->coef[2] < 0 && corr->coef[3] < 0);
		if (invert) {
			corr->coef[2] = -corr->coef[2];
			corr->coef[3] = -corr->coef[3];
		}

		// Need to use double and rint(), since calculation doesn't end
		// up on clean integer positions (i.e. 0.9999 can happen)
		joy.range_min = rint(center_min - ((32767.0 * 16384) / corr->coef[2]));
		joy.range_max = rint((32767.0 * 16384) / corr->coef[3] + center_max);

		printf("[JOYSTICK]: Invert: %i CenterMin: %i CenterMax: %i RangeMin: %i RangeMax: %i\n", invert, center_min, center_max, joy.range_min, joy.range_max);

		result = true;
	}

	free(corr);
	return result;
}

bool get_keycode_state(Display* display, uint keycode)
{
	char keys[32];
	XQueryKeymap(display, keys);
	return (keys[keycode / 8] & (0x1 << (keycode % 8)));
}

bool get_joystick_by_index(Joystick& joy, int i)
{
	char joy_path[sizeof("/dev/input/js1")]; // @Volatile_max_joy_count
	snprintf(joy_path, sizeof(joy_path), "/dev/input/js%i", i);

	if (get_joystick(joy, joy_path))
		return true;

	return false;
}

void get_joysticks(Joystick* const joysticks, int max_joy_count)
{
	for (int i = 0; i < max_joy_count; i++) {
		auto& joy = joysticks[i];
		get_joystick_by_index(joy, i);
	}
}

void fill_sound_buffer(SoundOutput& sound_output, const int frames_to_write)
{
	for (int i = 0; i < frames_to_write; i++) {
		auto sample_index = i * sound_output.channel_num;
		auto sine_value = sinf(sound_output.t_sine);
		i16 value = sine_value * sound_output.tone_volume;
		sound_output.sample_buffer[sample_index] = value;
		sound_output.sample_buffer[sample_index + 1] = value;
		sound_output.t_sine += M_PIf32 * 2.f / sound_output.wave_period();
	}

	auto frames_written = dy_snd_pcm_writei(sound_output.handle, sound_output.sample_buffer, frames_to_write);
	if (frames_written < 0) {
		frames_written = dy_snd_pcm_recover(sound_output.handle, frames_written, 0);
	}

	if (frames_written != frames_to_write) {
		printf("[ALSA]: Only wrote %ld frames (expected %d frames)\n", frames_written, frames_to_write);
	}

#if 0
	if (frames_written > 0) {
		snd_pcm_sframes_t delay, avail;
		dy_snd_pcm_avail_delay(sound_output.handle, &avail, &delay);
		printf("[ALSA]: Frames after write: %ld (+%ld), Frame delay: %ld\n", sound_output.frame_count() - avail + frames_written, frames_written, delay);
	}
#endif
}

static int fd, wd;
static bool is_running;

void sig_handler(int sig)
{
	is_running = false;
}

int main()
{
	signal(SIGINT, sig_handler);

	auto display = XOpenDisplay(0);

	if (!XShmQueryExtension(display)) {
		fprintf(stderr, "No XShm support\n");
		use_xshm = false;
	}

	auto screen = DefaultScreen(display);

	XVisualInfo vinfo;
	XMatchVisualInfo(display, screen, 24, TrueColor, &vinfo);

	ScreenBuffer buffer = {};
	if (!create_screen_buffer(buffer, 1280, 720, 32, vinfo, display))
		return 1;

	auto black_color = BlackPixel(display, screen);

	auto colormap = XCreateColormap(display, DefaultRootWindow(display), vinfo.visual, AllocNone);

	long event_mask = ExposureMask | StructureNotifyMask | ButtonPressMask | KeyPressMask | KeyReleaseMask;
	XSetWindowAttributes attrs = { .background_pixel = black_color, .bit_gravity = StaticGravity, .event_mask = event_mask, .colormap = colormap };
	unsigned long attrs_mask = CWColormap | CWBackPixel | CWEventMask | CWBitGravity;

	auto window = XCreateWindow(display, DefaultRootWindow(display), 0, 0, buffer.width, buffer.height, 0, vinfo.depth, InputOutput, vinfo.visual, attrs_mask, &attrs);
	if (!window) {
		fprintf(stderr, "Failed to XCreateWindow\n");
		delete_screen_buffer(buffer, display);
		return 1;
	}

#if 0
	XSizeHints hints = { .flags = PMinSize, .min_width = 800, .min_height = 500 };
	XSetStandardProperties(display, window, "My game", 0, 0, 0, 0, &hints);

#else
	XSizeHints hints = { .flags = PMinSize | PMaxSize, .min_width = buffer.width, .min_height = buffer.height, .max_width = buffer.width, .max_height = buffer.height };
	XSetStandardProperties(display, window, "My game", 0, 0, 0, 0, &hints);
#endif

	auto gc = XCreateGC(display, window, 0, 0);

	auto wm_delete_window_msg = XInternAtom(display, "WM_DELETE_WINDOW", 0);
	if (!XSetWMProtocols(display, window, &wm_delete_window_msg, 1)) {
		fprintf(stderr, "Couldn't register WM_DELETE_WINDOW property\n");
	}

	XMapRaised(display, window);

	const auto max_joy_count = 1; // @Volatile_max_joy_count
	Joystick joysticks[max_joy_count] = {};

	get_joysticks(joysticks, max_joy_count);

	{
		fd = inotify_init();
		if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
			fprintf(stderr, "Failed to fctnl\n");
		}

		auto path = "/dev/input";
		wd = inotify_add_watch(fd, path, IN_CREATE | IN_DELETE);
		if (wd != -1)
			printf("[INOTIFY]: Watching %s\n", path);
	}

	auto dl_name = "libasound.so";
	auto alsa_dl = dlopen(dl_name, RTLD_LAZY);
	if (!alsa_dl) {
		printf("[ALSA] Couldn't load %s\n", dl_name);
		return 0;
	}

	SoundOutput sound_output = {};
	if (!setup_alsa(sound_output, alsa_dl)) {
		printf("[ALSA]: Failed to init alsa\n");
	}

	dy_snd_pcm_writei = (snd_pcm_writei_fun*)dlsym(alsa_dl, "snd_pcm_writei");
	if (!dy_snd_pcm_writei) {
		printf("[ALSA]: Failed to init alsa\n");
	}

	dy_snd_pcm_recover = (snd_pcm_recover_fun*)dlsym(alsa_dl, "snd_pcm_recover");
	if (!dy_snd_pcm_recover) {
		printf("[ALSA]: Failed to init alsa\n");
	}

	dy_snd_pcm_avail_delay = (snd_pcm_avail_delay_fun*)dlsym(alsa_dl, "snd_pcm_avail_delay");
	if (!dy_snd_pcm_avail_delay) {
		printf("[ALSA]: Failed to init alsa\n");
	}

	dy_snd_pcm_drain = (snd_pcm_drain_fun*)dlsym(alsa_dl, "snd_pcm_drain");
	if (!dy_snd_pcm_drain) {
		printf("[ALSA]: Failed to init alsa\n");
	}

	dy_snd_pcm_close = (snd_pcm_close_fun*)dlsym(alsa_dl, "snd_pcm_close");
	if (!dy_snd_pcm_close) {
		printf("[ALSA]: Failed to init alsa\n");
	}

	sound_output.sample_buffer = (i16*)calloc(sound_output.byte_size(), 1); // @Volatile_bit_depth
	fill_sound_buffer(sound_output, sound_output.frame_rate / 15);

	auto x_offset = 0.f;
	auto y_offset = 0.f;
	auto up = 0;
	auto down = 0;
	auto left = 0;
	auto right = 0;

	XKeyEvent prev_key_event = {};
	bool key_is_pressed = false;

	auto buffer_size_changed = 0;
	auto timer_start = get_time_in_ns();
	is_running = true;
	while (is_running) {

		char ibuffer[BUF_LEN];

		auto length = read(fd, ibuffer, BUF_LEN);

		for (int i = 0; i < length;) {

			auto event = (inotify_event*)&ibuffer[i];

			if (event->len) {
				if ((event_mask & IN_ISDIR) == 0 && event->mask & (IN_CREATE | IN_DELETE)) {
					const char pattern[] = "js";
					auto pattern_len = sizeof(pattern) - 1;
					if (strncmp(event->name, pattern, pattern_len) == 0) {
						sleep(1); // @Hack
						if (event->mask & IN_CREATE) {
							printf("[INOTIFY]: %s was created\n", event->name);
							auto joy_index = strtol(event->name, 0, 10);

							if (joy_index < max_joy_count) {
								auto& joy = joysticks[joy_index];
								get_joystick_by_index(joy, joy_index);
							}

						} else if (event->mask & IN_DELETE) {
							printf("[INOTIFY]: %s was deleted\n", event->name);
							auto joy_index = strtol(event->name, 0, 10);

							if (joy_index < max_joy_count) {
								joysticks[joy_index] = {};
							}
						}
					}
				}
			}

			i += EVENT_SIZE + event->len;
		}

		for (int i = 0; i < max_joy_count; i++) {
			auto& joy = joysticks[i];
			js_event joy_event;
			while (joy.fd && read(joy.fd, &joy_event, sizeof(joy_event)) > 0) {
				if (joy_event.type & JS_EVENT_BUTTON && joy_event.value) {
					//printf("[JOYSTICK]: Button %i pressed\n", joy_event.number);
				} else if (joy_event.type & JS_EVENT_AXIS) {
					//printf("[JOYSTICK]: Axis %i updated with: %i\n", joy_event.number, joy_event.value);
					auto pos_threshold = joy.range_max / 5;
					auto neg_threshold = joy.range_min / 5;
					if (joy_event.number == 0) {
						if (joy_event.value > pos_threshold)
							joy.axis0 = (float)joy_event.value / joy.range_max;
						else if (joy_event.value < neg_threshold)
							joy.axis0 = -(float)joy_event.value / joy.range_min;
						else
							joy.axis0 = 0;
					} else if (joy_event.number == 1) {
						if (joy_event.value > pos_threshold)
							joy.axis1 = (float)joy_event.value / joy.range_max;
						else if (joy_event.value < neg_threshold)
							joy.axis1 = -(float)joy_event.value / joy.range_min;
						else
							joy.axis1 = 0;
					}
				}
			}
		}

		while (XPending(display) > 0) {

			XEvent event;
			XNextEvent(display, &event);
			switch (event.type) {
			case DestroyNotify: {
				is_running = false;
				printf("DestroyNotify\n");
			} break;
			case ClientMessage: {
				auto& msg = *(XClientMessageEvent*)&event;
				if (msg.data.l[0] == wm_delete_window_msg) {
					is_running = false;
					printf("[MSG]: ClientMessage\n");
				}
			} break;
			case ButtonPress: {
				printf("You pressed a button at (%i, %i)\n", event.xbutton.x, event.xbutton.y);
			} break;
			case KeyRelease: // fall through
			case KeyPress: {
				auto& key_event = *(XKeyEvent*)&event;

#if 0
				if (key_event.type == KeyPress)
					printf("%i was Pressed at %ld\n", key_event.keycode, key_event.time);
				else if (key_event.type == KeyRelease)
					printf("%i was Released at %ld\n", key_event.keycode, key_event.time);
#endif
#if 0
				auto was_pressed = key_event.type == KeyRelease || (prev_key_event.type == KeyRelease && prev_key_event.time == key_event.time && prev_key_event.keycode == key_event.keycode);
				auto is_pressed = key_event.type == KeyPress || get_keycode_state(display, key_event.keycode);
				prev_key_event = key_event;

				if (was_pressed != is_pressed) {
#endif
				auto is_pressed = key_event.type == KeyPress;
				auto keysym = XLookupKeysym(&key_event, 0);
				switch (keysym) {
				case XK_w:
					up = is_pressed;
					break;
				case XK_a:
					left = is_pressed;
					break;
				case XK_s:
					down = is_pressed;
					break;
				case XK_d:
					right = is_pressed;
					break;
				}

			} break;
			case ConfigureNotify: {
				auto& msg = *(XConfigureEvent*)&event;

				if (msg.width != buffer.width) {
					buffer.width = msg.width;
					buffer_size_changed = 1;
				}
				if (msg.height != buffer.height) {
					buffer.height = msg.height;
					buffer_size_changed = 1;
				}

			} break;
			}
		}

		auto& joy = joysticks[0];

		x_offset += joy.axis0 * 1.5 + (right - left) * 1.5;
		y_offset += joy.axis1 * 1.5 + (down - up) * 1.5;

		sound_output.hz = 512.f + joy.axis1 * 256.f;

		if (buffer_size_changed) {
			printf("BufferSizeChanged\n");
			delete_screen_buffer(buffer, display);
			if (!create_screen_buffer(buffer, buffer.width, buffer.height, 32, vinfo, display)) {
				delete_screen_buffer(buffer, display);
				return 1;
			}
			buffer_size_changed = 0;
		}

		for (int y = 0; y < buffer.height; y++) {
			auto row = buffer.buffer + (y * buffer.pitch());
			for (int x = 0; x < buffer.width; x++) {
				auto p = (u32*)(row + (x * buffer.pixel_bytes()));

				*p = (u8)(y + y_offset) << 8 | (u8)(x + x_offset);
			}
		}

		auto expected_sound_frames_per_video_frame = sound_output.frame_rate / 20;

		static int printf_timer = 0;
		printf_timer++;

		snd_pcm_sframes_t delay, avail;
		dy_snd_pcm_avail_delay(sound_output.handle, &avail, &delay);
		expected_sound_frames_per_video_frame -= delay;

		auto frames_to_write = expected_sound_frames_per_video_frame > avail ? avail : expected_sound_frames_per_video_frame;
		if (frames_to_write < 0)
			frames_to_write = 0;

		if (printf_timer % 100 == 0) {
			printf("[ALSA]: Delay: %.3fs, Avail: %.3fs, Expected: %.3fs, Filling: %.3fs\n", (float)delay / (float)sound_output.frame_rate, (float)avail / (float)sound_output.frame_rate, (float)expected_sound_frames_per_video_frame / (float)sound_output.frame_rate, (float)frames_to_write / (float)sound_output.frame_rate);
		}

		fill_sound_buffer(sound_output, frames_to_write);

		if (use_xshm) {
			XShmPutImage(display, window, gc, buffer.ximage, 0, 0, 0, 0, buffer.width, buffer.height, 0);
			XFlush(display);
		} else {
			XPutImage(display, window, gc, buffer.ximage, 0, 0, 0, 0, buffer.width, buffer.height);
		}

		const auto timer_end = get_time_in_ns();
		const auto time_elapsed_ns = timer_end - timer_start;
		timer_start = timer_end;

#if FPS
		if (time_elapsed_ns > 0 && printf_timer % 100 == 0)
			printf("[PERF]: %.2fms %ifps\n", time_elapsed_ns / 1e6, (int)(1e9 / time_elapsed_ns));
#endif
	}

	//dy_snd_pcm_drain(sound_output.handle);
	//dy_snd_pcm_close(sound_output.handle);

	delete_screen_buffer(buffer, display);
	inotify_rm_watch(fd, wd);
	close(fd);

	printf("END OF THE PROGRAM!\n");
}
