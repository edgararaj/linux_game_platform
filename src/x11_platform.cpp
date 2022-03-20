#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <malloc.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <x86intrin.h>

#include "game.cpp"
#include "game.h"
#include "linux_alsa.cpp"
#include "linux_file_io.cpp"
#include "linux_joystick.cpp"
#include "types.h"

#define ALSA_DEBUG 0
#define FPS 1

auto use_xshm = true;

int get_ns_time()
{
	timespec spec;
	clock_gettime(CLOCK_MONOTONIC, &spec);
	return spec.tv_nsec;
}

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

bool get_keycode_state(Display* display, uint keycode)
{
	char keys[32];
	XQueryKeymap(display, keys);
	return (keys[keycode / 8] & (0x1 << (keycode % 8)));
}

void write_sound_buffer(SoundOutput& sound_output, const int frames_to_write)
{
	auto frames_written = snd_pcm_writei(sound_output.handle, sound_output.sample_buffer, frames_to_write);
	if (frames_written < 0) {
		frames_written = snd_pcm_recover(sound_output.handle, frames_written, 0);
	}

	if (frames_written != frames_to_write) {
		printf("[ALSA]: Only wrote %ld frames (expected %d frames)\n", frames_written, frames_to_write);
	}

#if 0
	if (frames_written > 0) {
		snd_pcm_sframes_t delay, avail;
		snd_pcm_avail_delay(sound_output.handle, &avail, &delay);
		printf("[ALSA]: Frames after write: %ld (+%ld), Frame delay: %ld\n", sound_output.frame_count() - avail + frames_written, frames_written, delay);
	}
#endif
}

void process_joy_axis_event(float state, float& new_state, const float& prev_state)
{
	new_state = state;
}

void process_joy_btn_event(int btn, const js_event& event, GameBtnState& new_state, const GameBtnState& prev_state)
{
	if (event.number == btn) {
		new_state.ended_down = event.value;
		new_state.half_trans_count = prev_state.ended_down != event.value;
	}
}

void process_keyboard_event(bool state, GameBtnState& new_state)
{
	new_state.ended_down = state;
	new_state.half_trans_count++;
}

auto is_running = true;

void x11_process_input_msgs(const XEvent& event, GameCtrlInput& keyboard_ctrl)
{
	switch (event.type) {
	case ButtonPress: {
		printf("You pressed a button at (%i, %i)\n", event.xbutton.x, event.xbutton.y);
	} break;
	case KeyRelease: // fall through
	case KeyPress: {
		for (auto& btn : keyboard_ctrl.buttons) {
			btn.half_trans_count = 0;
		}
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
			process_keyboard_event(is_pressed, keyboard_ctrl.up);
			break;
		case XK_a:
			process_keyboard_event(is_pressed, keyboard_ctrl.left);
			break;
		case XK_s:
			process_keyboard_event(is_pressed, keyboard_ctrl.down);
			break;
		case XK_d:
			process_keyboard_event(is_pressed, keyboard_ctrl.right);
			break;
		case XK_q:
			process_keyboard_event(is_pressed, keyboard_ctrl.lb);
			break;
		case XK_e:
			process_keyboard_event(is_pressed, keyboard_ctrl.rb);
			break;
		}

	} break;
	}
}

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

	void* base_addr =
#if INTERNAL
		(void*)GiB(3)
#else
		0
#endif
		;

	GameMemory game_memory = {};
	game_memory.perm_storage_size = MiB(64);
	game_memory.trans_storage_size = GiB(2);
	game_memory.perm_storage = mmap(base_addr, game_memory.perm_storage_size + game_memory.trans_storage_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	printf("mmap on %p\n", game_memory.perm_storage);
	game_memory.trans_storage = (u8*)game_memory.perm_storage + game_memory.trans_storage_size;

	if (!game_memory.perm_storage || game_memory.perm_storage == MAP_FAILED
		|| !game_memory.trans_storage || game_memory.trans_storage == MAP_FAILED) {
		fprintf(stderr, "Failed to allocate game memory: %s!\n", strerror(errno));
		return 1;
	}

	GameInput inputs[2] = {};
	auto& prev_input = inputs[0];
	auto& new_input = inputs[1];

	Joystick joysticks[max_joy_count] = {};
	JoystickInotify joystick_inotify;

	get_joysticks(joysticks, max_joy_count);

	joystick_inotify_setup(joystick_inotify);

	SoundOutput sound_output = {};
	if (!alsa_setup(sound_output)) {
		printf("[ALSA]: Failed to init alsa\n");
	}

	sound_output.sample_buffer = (i16*)calloc(sound_output.byte_size(), 1); // @Volatile_bit_depth
	write_sound_buffer(sound_output, sound_output.frame_rate / 15);

	//	XKeyEvent prev_key_event = {};
	//	bool key_is_pressed = false;

	auto buffer_size_changed = false;
	auto timer_start = get_ns_time();
	auto cycle_count_start = __rdtsc();
	is_running = true;
	while (is_running) {

		joystick_inotify_update(joystick_inotify, joysticks, max_joy_count);

		for (int i = 0; i < max_joy_count; i++) {
			const auto ctrl_index = i + max_keyboard_count;
			const auto& prev_ctrl = prev_input.ctrls[ctrl_index];
			auto& new_ctrl = new_input.ctrls[ctrl_index];
			auto& joy = joysticks[i];
			js_event joy_event;
			while (joy.fd && read(joy.fd, &joy_event, sizeof(joy_event)) > 0) {
				if (joy_event.type & JS_EVENT_BUTTON) {

					// printf("[JOYSTICK]: Button %i %s\n", joy_event.number, joy_event.value ? "pressed" : "released");
					process_joy_btn_event(0, joy_event, new_ctrl.down, prev_ctrl.down);
					process_joy_btn_event(1, joy_event, new_ctrl.right, prev_ctrl.right);
					process_joy_btn_event(2, joy_event, new_ctrl.left, prev_ctrl.left);
					process_joy_btn_event(3, joy_event, new_ctrl.up, prev_ctrl.up);
					process_joy_btn_event(4, joy_event, new_ctrl.lb, prev_ctrl.lb);
					process_joy_btn_event(5, joy_event, new_ctrl.rb, prev_ctrl.rb);

				} else if (joy_event.type & JS_EVENT_AXIS) {

					// printf("[JOYSTICK]: Axis %i updated with: %i\n", joy_event.number, joy_event.value);
					auto pos_threshold = joy.range_max / 5;
					auto neg_threshold = joy.range_min / 5;
					if (joy_event.number == 0) {
						float joy_x;
						if (joy_event.value > pos_threshold)
							joy_x = (float)joy_event.value / joy.range_max;
						else if (joy_event.value < neg_threshold)
							joy_x = -(float)joy_event.value / joy.range_min;
						else
							joy_x = 0;

						new_ctrl.end_x = joy_x;

					} else if (joy_event.number == 1) {
						float joy_y;
						if (joy_event.value > pos_threshold)
							joy_y = (float)joy_event.value / joy.range_max;
						else if (joy_event.value < neg_threshold)
							joy_y = -(float)joy_event.value / joy.range_min;
						else
							joy_y = 0;

						new_ctrl.end_y = joy_y;
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
				if (msg.data.l[0] == (long)wm_delete_window_msg) {
					is_running = false;
					printf("[MSG]: ClientMessage\n");
				}
			} break;
			case ConfigureNotify: {
				auto& msg = *(XConfigureEvent*)&event;

				if (msg.width != buffer.width) {
					buffer.width = msg.width;
					buffer_size_changed = true;
				}
				if (msg.height != buffer.height) {
					buffer.height = msg.height;
					buffer_size_changed = true;
				}
			} break;
			default:
				x11_process_input_msgs(event, new_input.ctrls[0]);
			}
		}
		if (buffer_size_changed) {
			printf("BufferSizeChanged\n");
			delete_screen_buffer(buffer, display);
			if (!create_screen_buffer(buffer, buffer.width, buffer.height, 32, vinfo, display)) {
				delete_screen_buffer(buffer, display);
				return 1;
			}
			buffer_size_changed = false;
		}

		auto expected_sound_frames_per_video_frame = sound_output.frame_rate / 20;

		snd_pcm_sframes_t delay, avail;
		snd_pcm_avail_delay(sound_output.handle, &avail, &delay);
		expected_sound_frames_per_video_frame -= delay;

		auto frames_to_write = expected_sound_frames_per_video_frame > avail ? avail : expected_sound_frames_per_video_frame;
		if (frames_to_write < 0)
			frames_to_write = 0;

		GameScreenBuffer game_buffer = { .width = buffer.width, .height = buffer.height, .pixel_bits = buffer.pixel_bits, .buffer = buffer.buffer };
		GameSoundBuffer game_sound_buffer = { .frame_rate = sound_output.frame_rate, .channel_num = sound_output.channel_num, .sample_buffer = sound_output.sample_buffer, .frame_count = frames_to_write };

		game_update_and_render(game_memory, game_buffer, game_sound_buffer, new_input);
		write_sound_buffer(sound_output, frames_to_write);

		if (use_xshm) {
			XShmPutImage(display, window, gc, buffer.ximage, 0, 0, 0, 0, buffer.width, buffer.height, 0);
			XFlush(display);
		} else {
			XPutImage(display, window, gc, buffer.ximage, 0, 0, 0, 0, buffer.width, buffer.height);
		}

		std::swap(new_input, prev_input);

		static int printf_timer = 0;
		printf_timer++;

#if ALSA_DEBUG
		if (printf_timer % 100 == 0) {
			const auto log_delay = (float)delay / (float)sound_output.frame_rate;
			const auto log_avail = (float)avail / (float)sound_output.frame_rate;
			const auto log_expected = (float)expected_sound_frames_per_video_frame / (float)sound_output.frame_rate;
			const auto log_filling = (float)frames_to_write / (float)sound_output.frame_rate;
			printf("[ALSA]: Delay: %.3fs, Avail: %.3fs, Expected: %.3fs, Filling: %.3fs\n", log_delay, log_avail, log_expected, log_filling);
		}
#endif

		const auto timer_end = get_ns_time();
		const auto cycle_count_end = __rdtsc();
		const auto ns_elapsed = timer_end - timer_start;
		const auto cycles_elapsed = cycle_count_end - cycle_count_start;
#if FPS
		if (ns_elapsed > 0 && printf_timer % 100 == 0)
			printf("[PERF]: %.2fms %ifps %.2fmc\n", ns_elapsed / 1e6, (int)(1e9 / ns_elapsed), cycles_elapsed / 1e6);
#endif

		timer_start = timer_end;
		cycle_count_start = cycle_count_end;
	}

	// snd_pcm_drain(sound_output.handle);
	// snd_pcm_close(sound_output.handle);

	delete_screen_buffer(buffer, display);
	joystick_inotify_close(joystick_inotify);

	printf("END OF THE PROGRAM!\n");
}
