#include "optional.cpp"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#define countof(x) (sizeof(x) / sizeof((x)[0]))

int main()
{
	auto display = XOpenDisplay(0);

	if (!XShmQueryExtension(display)) {
		fprintf(stderr, "No XShm support\n");
		return 1;
	}

	auto screen = DefaultScreen(display);

	XVisualInfo vinfo;
	XMatchVisualInfo(display, screen, 24, TrueColor, &vinfo);

	const auto width = 1280;
	const auto height = 720;
	XShmSegmentInfo shminfo;

	auto ximage = XShmCreateImage(display, vinfo.visual, vinfo.depth, ZPixmap, 0, &shminfo, width, height);
	shminfo.shmid = shmget(IPC_PRIVATE, ximage->bytes_per_line * ximage->height, IPC_CREAT | 0777);
	shminfo.shmaddr = ximage->data = (char*)shmat(shminfo.shmid, 0, 0);
	shminfo.readOnly = 1;
	if (!XShmAttach(display, &shminfo)) {
		fprintf(stderr, "Failed to XShmAttach\n");
		return 1;
	}

	printf("MIT-SHM: Shared memory KID=%d, at=%p\n", shminfo.shmid, shminfo.shmaddr);

	auto black_color = BlackPixel(display, screen);

	auto colormap = XCreateColormap(display, DefaultRootWindow(display), vinfo.visual, AllocNone);
	XSetWindowAttributes attrs = { .background_pixel = black_color, .event_mask = ExposureMask | StructureNotifyMask | ButtonPressMask | KeyPressMask, .colormap = colormap };
	unsigned long attrs_mask = CWColormap | CWBackPixel | CWEventMask;

	auto window = XCreateWindow(display, DefaultRootWindow(display), 0, 0, width, height, 0, vinfo.depth, InputOutput, vinfo.visual, attrs_mask, &attrs);
	if (!window) {
		fprintf(stderr, "Failed to XCreateWindow\n");
		return 1;
	}

	XSizeHints hints = { .flags = PMinSize | PMaxSize, .min_width = width, .min_height = height, .max_width = width, .max_height = height };
	XSetStandardProperties(display, window, "My game", 0, 0, 0, 0, &hints);

	auto gc = XCreateGC(display, window, 0, 0);

#if 0
	XColor colors[255];

	for (int i = 0; i < countof(colors); i++) {
		colors[i].flags = DoRed | DoGreen | DoBlue;
		colors[i].red = i * 256;
		colors[i].green = i * 256;
		colors[i].blue = i * 256;
		colors[i].pixel = i;
	}
	XStoreColors(display, colormap, colors, countof(colors));
	XSetWindowColormap(display, window, colormap);
#endif

#if 0
	auto colormap = XCreateColormap(display, window, visual, AllocAll);

	XColor xcolor[256];
	for (int i = 0; i < 256; i++) {
		xcolor[i].flags = DoRed | DoGreen | DoBlue;
		xcolor[i].red = (i) << 9;
		xcolor[i].green = (i) << 9;
		xcolor[i].blue = (i) << 9;
		xcolor[i].pixel = i;
		XStoreColor(display, colormap, &xcolor[i]);
	}

	XInstallColormap(display, colormap);
	XSetWindowColormap(display, window, colormap);
#endif

	XClearWindow(display, window);
	XMapRaised(display, window);

	auto wm_delete_window_msg = XInternAtom(display, "WM_DELETE_WINDOW", 0);
	if (!XSetWMProtocols(display, window, &wm_delete_window_msg, 1)) {
		fprintf(stderr, "Couldn't register WM_DELETE_WINDOW property\n");
	}

	auto is_running = 1;
	while (is_running) {
		XEvent event;
		while (XPending(display) > 0) {

			XNextEvent(display, &event);
			switch (event.type) {
			case DestroyNotify:
				is_running = 0;
				printf("DestroyNotify\n");
				break;
			case ClientMessage: {
				XClientMessageEvent* msg = (XClientMessageEvent*)&event;
				if (msg->data.l[0] == wm_delete_window_msg) {
					is_running = 0;
					printf("ClientMessage\n");
				}
			} break;
			case ButtonPress:
				printf("You pressed a button at (%i, %i)\n", event.xbutton.x, event.xbutton.y);
				break;
			}
		}

		printf("arroz\n");

		for (int x = 0; x < height; x++) {
			for (int y = 0; y < width; y++) {
				ximage->data[y * width + x] = x + 25;
			}
		}

		XShmPutImage(display, window, gc, ximage, 0, 0, 0, 0, width, height, 0);
	}

	// Cleanup
	XShmDetach(display, &shminfo);
	XDestroyImage(ximage);
	shmdt(shminfo.shmaddr);
	shmctl(shminfo.shmid, IPC_RMID, 0);

	printf("ola eu sou o edgar\n");
}
