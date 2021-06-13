#include "optional.cpp"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <fcntl.h>
#include <linux/joystick.h>
#include <malloc.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

#define countof(x) (sizeof(x) / sizeof((x)[0]))

auto use_xshm = true;

int get_time_in_ns()
{
	timespec spec;
	clock_gettime(CLOCK_MONOTONIC, &spec);

	return spec.tv_nsec;
}

struct ScreenBuffer {
	int width;
	int height;
	int pixel_bits;
	char* mem;
	XImage* ximage;
	XShmSegmentInfo shminfo;
	int pixel_bytes() const
	{
		return pixel_bits / 8;
	}
	int byte_size() const { return width * height * pixel_bytes(); }
	int pitch() const { return width * pixel_bytes(); }
};

void delete_buffer(ScreenBuffer& buffer, Display* display)
{
	if (use_xshm) {
		XShmDetach(display, &buffer.shminfo);
		shmdt(buffer.shminfo.shmaddr);
	}
	XDestroyImage(buffer.ximage);
}

bool create_buffer(ScreenBuffer& buffer, int width, int height, int pixel_bits, const XVisualInfo& vinfo, Display* display)
{
	buffer.width = width;
	buffer.height = height;
	buffer.pixel_bits = pixel_bits;

	if (use_xshm) {
		// TODO: Cant create a new shared buffer when resized, it crashes the app, maybe allocate a large one and use the portion needed
		buffer.ximage = XShmCreateImage(display, vinfo.visual, vinfo.depth, ZPixmap, 0, &buffer.shminfo, width, height);
		buffer.shminfo.shmid = shmget(IPC_PRIVATE, buffer.ximage->bytes_per_line * buffer.ximage->height, IPC_CREAT | 0777);
		buffer.mem = buffer.shminfo.shmaddr = buffer.ximage->data = (char*)shmat(buffer.shminfo.shmid, 0, 0);
		buffer.shminfo.readOnly = 1;

		if (!XShmAttach(display, &buffer.shminfo)) {
			fprintf(stderr, "Failed to XShmAttach\n");
			return 0;
		}

		shmctl(buffer.shminfo.shmid, IPC_RMID, 0); // Mark shared buffer for removal after process end; cant do on create_buffer because it crashes

		printf("[MIT-SHM]: Shared memory KID=%d, at=%p\n", buffer.shminfo.shmid, buffer.shminfo.shmaddr);

	} else {
		buffer.mem = (char*)malloc(buffer.byte_size());
		if (!buffer.mem) {
			fprintf(stderr, "Failed to malloc\n");
			return 0;
		}

		buffer.ximage = XCreateImage(display, vinfo.visual, vinfo.depth, ZPixmap, 0, buffer.mem, width, height, pixel_bits, 0);
	}

	return 1;
}

struct JoyCalibrationData {
	bool calibrate;
	bool invert;
	int center_min;
	int center_max;
	int range_min;
	int range_max;
};

int main()
{
	auto display = XOpenDisplay(0);

	if (!XShmQueryExtension(display)) {
		fprintf(stderr, "No XShm support\n");
		use_xshm = false;
	}

	auto screen = DefaultScreen(display);

	XVisualInfo vinfo;
	XMatchVisualInfo(display, screen, 24, TrueColor, &vinfo);

	ScreenBuffer buffer = {};
	if (!create_buffer(buffer, 1280, 720, 32, vinfo, display))
		return 1;

	auto black_color = BlackPixel(display, screen);

	auto colormap = XCreateColormap(display, DefaultRootWindow(display), vinfo.visual, AllocNone);

	long event_mask = ExposureMask | StructureNotifyMask | ButtonPressMask | KeyPressMask | KeyReleaseMask;
	XSetWindowAttributes attrs = { .background_pixel = black_color, .bit_gravity = StaticGravity, .event_mask = event_mask, .colormap = colormap };
	unsigned long attrs_mask = CWColormap | CWBackPixel | CWEventMask | CWBitGravity;

	auto window = XCreateWindow(display, DefaultRootWindow(display), 0, 0, buffer.width, buffer.height, 0, vinfo.depth, InputOutput, vinfo.visual, attrs_mask, &attrs);
	if (!window) {
		fprintf(stderr, "Failed to XCreateWindow\n");
		delete_buffer(buffer, display);
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

	const auto max_joy_count = 1; // @Volatile
	auto joy_fd_count = 0;
	int joy_fds[max_joy_count];
	JoyCalibrationData joy_datas[max_joy_count] = {};

	for (int i = 0; i < max_joy_count; i++) {
		char joy_path[sizeof("/dev/input/js1")]; // @Volatile
		snprintf(joy_path, sizeof(joy_path), "/dev/input/js%i", i);

		auto& joy_fd = joy_fds[i];
		joy_fd = open(joy_path, O_RDONLY | O_NONBLOCK);
		if (joy_fd < 0) {
			fprintf(stderr, "Couldn't open %s\n", joy_path);
			continue;
		}

		joy_fd_count++;

		u8 num_axis = 0;
		u8 num_buttons = 0;
		ioctl(joy_fd, JSIOCGAXES, &num_axis);
		ioctl(joy_fd, JSIOCGBUTTONS, &num_buttons);

		char name[1024];
		if (ioctl(joy_fd, JSIOCGNAME(sizeof(name)), name) < 0) {
			fprintf(stderr, "Couldn't get %s name\n", joy_path);
		}

		printf("[JOYSTICK]: %s is connected (Axis: %i, Buttons: %i)\n", name, num_axis, num_buttons);

		auto corr = (js_corr*)malloc(num_axis * sizeof(js_corr));
		if (ioctl(joy_fd, JSIOCGCORR, corr) < 0) {
			fprintf(stderr, "Couldn't get %s calibration\n", name);
		}

		auto& data = joy_datas[i];
		if (corr->type) {
			data.calibrate = true;
			data.invert = (corr->coef[2] < 0 && corr->coef[3] < 0);
			data.center_min = corr->coef[0];
			data.center_max = corr->coef[1];

			if (data.invert) {
				corr->coef[2] = -corr->coef[2];
				corr->coef[3] = -corr->coef[3];
			}

			// Need to use double and rint(), since calculation doesn't end
			// up on clean integer positions (i.e. 0.9999 can happen)
			data.range_min = rint(data.center_min - ((32767.0 * 16384) / corr->coef[2]));
			data.range_max = rint((32767.0 * 16384) / corr->coef[3] + data.center_max);

			printf("[JOYSTICK]: Invert: %i CenterMin: %i CenterMax: %i RangeMin: %i RangeMax: %i\n", data.invert, data.center_min, data.center_max, data.range_min, data.range_max);
		}
	}

	auto x_offset = 0;
	auto y_offset = 0;
	auto axis0 = 0;
	auto axis1 = 0;

	auto buffer_size_changed = 0;
	auto timer_start = get_time_in_ns();
	auto is_running = true;
	while (is_running) {

		js_event joy_event;

		for (int i = 0; i < joy_fd_count; i++) {
			auto joy_fd = joy_fds[i];
			while (read(joy_fd, &joy_event, sizeof(joy_event)) > 0) {
				auto data = joy_datas[i];
				if (joy_event.type & JS_EVENT_BUTTON && joy_event.value) {
					//printf("[JOYSTICK]: Button %i pressed\n", joy_event.number);
				} else if (joy_event.type & JS_EVENT_AXIS) {
					//printf("[JOYSTICK]: Axis %i updated with: %i\n", joy_event.number, joy_event.value);
					auto pos_threshold = data.range_max / 3;
					auto neg_threshold = data.range_min / 3;
					if (joy_event.number == 0) {
						if (joy_event.value > pos_threshold)
							axis0 = 1;
						else if (joy_event.value < neg_threshold)
							axis0 = -1;
						else
							axis0 = 0;
					} else if (joy_event.number == 1) {
						if (joy_event.value > pos_threshold)
							axis1 = 1;
						else if (joy_event.value < neg_threshold)
							axis1 = -1;
						else
							axis1 = 0;
					}
				}
			}
		}

		x_offset += axis0;
		y_offset += axis1;

		XEvent event;
		while (XPending(display) > 0) {

			XNextEvent(display, &event);
			switch (event.type) {
			case DestroyNotify: {
				is_running = false;
				printf("DestroyNotify\n");
			} break;
			case ClientMessage: {
				auto msg = (XClientMessageEvent*)&event;
				if (msg->data.l[0] == wm_delete_window_msg) {
					is_running = 0;
					printf("[MSG]: ClientMessage\n");
				}
			} break;
			case ButtonPress: {
				printf("You pressed a button at (%i, %i)\n", event.xbutton.x, event.xbutton.y);
			} break;
			case KeyPress: {
				auto msg = (XKeyPressedEvent*)&event;
				if (msg->keycode == XKeysymToKeycode(display, XK_W))
					y_offset -= 5;
				if (msg->keycode == XKeysymToKeycode(display, XK_A))
					x_offset -= 5;
				if (msg->keycode == XKeysymToKeycode(display, XK_S))
					y_offset += 5;
				if (msg->keycode == XKeysymToKeycode(display, XK_D))
					x_offset += 5;
			} break;
			case ConfigureNotify: {
				printf("[MSG]: ConfigureNotify\n");
				auto msg = (XConfigureEvent*)&event;

				if (msg->width != buffer.width) {
					buffer.width = msg->width;
					buffer_size_changed = 1;
				}
				if (msg->height != buffer.height) {
					buffer.height = msg->height;
					buffer_size_changed = 1;
				}

			} break;
			}
		}

		if (buffer_size_changed) {
			printf("BufferSizeChanged\n");
			delete_buffer(buffer, display);
			if (!create_buffer(buffer, buffer.width, buffer.height, 32, vinfo, display)) {
				delete_buffer(buffer, display);
				return 1;
			}
			buffer_size_changed = 0;
		}

		for (int y = 0; y < buffer.height; y++) {
			auto row = buffer.mem + (y * buffer.pitch());
			for (int x = 0; x < buffer.width; x++) {
				auto p = (u32*)(row + (x * buffer.pixel_bytes()));

				*p = (u8)(y + y_offset) << 8 | (u8)(x + x_offset);
			}
		}

		if (use_xshm) {
			XShmPutImage(display, window, gc, buffer.ximage, 0, 0, 0, 0, buffer.width, buffer.height, 0);
			XFlush(display);
		} else {
			XPutImage(display, window, gc, buffer.ximage, 0, 0, 0, 0, buffer.width, buffer.height);
		}

		const auto timer_end = get_time_in_ns();
		const auto time_elapsed_ns = timer_end - timer_start;
		timer_start = timer_end;

#if 0
		if (time_elapsed_ns > 0)
			printf("[PERF]: %.2fms %ifps\n", time_elapsed_ns / 1e6, (int)(1e9 / time_elapsed_ns));
#endif
	}

	delete_buffer(buffer, display);

	printf("ola eu sou o edgar\n");
}
