#include "optional.cpp"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/shm.h>

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

	auto width = 1280;
	auto height = 720;
	XShmSegmentInfo shminfo;

	auto ximage = XShmCreateImage(display, vinfo.visual, 24, ZPixmap, 0, &shminfo, width, height);
	shminfo.shmid = shmget(IPC_PRIVATE, ximage->bytes_per_line * ximage->height, IPC_CREAT | 0777);
	shminfo.shmaddr = ximage->data = (char*)shmat(shminfo.shmid, 0, 0);
	shminfo.readOnly = 0;
	if (!XShmAttach(display, &shminfo)) {
		fprintf(stderr, "Failed to XShmAttach\n");
		return 1;
	}

	auto black_color = BlackPixel(display, screen);
	auto white_color = WhitePixel(display, screen);

	XSetWindowAttributes attrs = { .background_pixel = white_color, .border_pixel = black_color, .event_mask = 0 };
	unsigned long attrs_mask = CWBackPixel | CWBorderPixel | CWEventMask;
	auto window = XCreateWindow(display, DefaultRootWindow(display), 0, 0, width, height, 0, CopyFromParent, CopyFromParent /* or InputOutput */, CopyFromParent, attrs_mask, &attrs);
	if (!window) {
		fprintf(stderr, "Failed to XCreateWindow\n");
		return 1;
	}

	XSetStandardProperties(display, window, "My game", 0, 0, 0, 0, 0);

	printf("MIT-SHM: Shared memory KID=%d, at=%p\n", shminfo.shmid, shminfo.shmaddr);

	XSelectInput(display, window, ExposureMask | ButtonPressMask | KeyPressMask);

	auto gc = XCreateGC(display, window, 0, 0);

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

	//XClearWindow(display, window);
	XMapRaised(display, window);

	XEvent event;
	while (1) {
		XNextEvent(display, &event);
		if (event.type == ButtonPress) {
			printf("You pressed a button at (%i, %i)\n", event.xbutton.x, event.xbutton.y);
		}
		for (int x = 0; x < 50; x++) {
			for (int y = 0; y < 50; y++) {
				ximage->data[y * width + x] = 255;
			}
		}

		XShmPutImage(display, window, gc, ximage, 0, 0, 0, 0, width, height, 1);
	}

	// Cleanup
	XShmDetach(display, &shminfo);
	XDestroyImage(ximage);
	shmdt(shminfo.shmaddr);
	shmctl(shminfo.shmid, IPC_RMID, 0);

	printf("ola eu sou o edgar\n");
}
