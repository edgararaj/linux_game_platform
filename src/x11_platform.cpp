#include "optional.cpp"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <time.h>

#define countof(x) (sizeof(x) / sizeof((x)[0]))

auto use_xshm = 1;

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

		printf("MIT-SHM: Shared memory KID=%d, at=%p\n", buffer.shminfo.shmid, buffer.shminfo.shmaddr);

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

int main()
{
	auto display = XOpenDisplay(0);

	if (!XShmQueryExtension(display)) {
		fprintf(stderr, "No XShm support\n");
		use_xshm = 0;
	}

	auto screen = DefaultScreen(display);

	XVisualInfo vinfo;
	XMatchVisualInfo(display, screen, 24, TrueColor, &vinfo);

	ScreenBuffer buffer = {};
	if (!create_buffer(buffer, 1280, 720, 32, vinfo, display))
		return 1;

	auto black_color = BlackPixel(display, screen);

	auto colormap = XCreateColormap(display, DefaultRootWindow(display), vinfo.visual, AllocNone);
	XSetWindowAttributes attrs = { .background_pixel = black_color, .bit_gravity = StaticGravity, .event_mask = ExposureMask | StructureNotifyMask | ButtonPressMask | KeyPressMask, .colormap = colormap };
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

	XClearWindow(display, window);
	XMapRaised(display, window);

	auto wm_delete_window_msg = XInternAtom(display, "WM_DELETE_WINDOW", 0);
	if (!XSetWMProtocols(display, window, &wm_delete_window_msg, 1)) {
		fprintf(stderr, "Couldn't register WM_DELETE_WINDOW property\n");
	}

	auto buffer_size_changed = 0;
	auto timer_start = get_time_in_ns();
	auto is_running = 1;
	while (is_running) {
		XEvent event;
		while (XPending(display) > 0) {

			XNextEvent(display, &event);
			switch (event.type) {
			case DestroyNotify: {
				is_running = 0;
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

		static unsigned long long move = 0;
		move += 4;

		for (int y = 0; y < buffer.height; y++) {
			auto row = buffer.mem + (y * buffer.pitch());
			for (int x = 0; x < buffer.width; x++) {
				auto p = (unsigned int*)(row + (x * buffer.pixel_bytes()));
				const auto width_flag = 150;

				const auto new_x = (x + move) % width_flag;

				if (new_x < 50)
					*p = 0x00ffddff * move;
				else if (new_x < 150)
					*p = 0x00ddffff;
			}
		}

		if (use_xshm) {
			XShmPutImage(display, window, gc, buffer.ximage, 0, 0, 0, 0, buffer.width, buffer.height, 0);
		} else {
			XPutImage(display, window, gc, buffer.ximage, 0, 0, 0, 0, buffer.width, buffer.height);
		}

		const auto timer_end = get_time_in_ns();
		const auto time_elapsed_ns = timer_end - timer_start;
		timer_start = timer_end;

		if (time_elapsed_ns > 0)
			printf("[PERF]: %.2fms %ifps\n", time_elapsed_ns / 1e6, (int)(1e9 / time_elapsed_ns));
	}

	delete_buffer(buffer, display);

	printf("ola eu sou o edgar\n");
}
