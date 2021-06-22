#include "optional.cpp"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xdbe.h>
#include <stdio.h>

Optional<Visual*>
get_xdbe_visual(Display* display)
{
	int major, minor;
	if (!XdbeQueryExtension(display, &major, &minor)) {
		fprintf(stderr, "No Xdbe support\n");
		return {};
	}

	printf("Xdbe (%d.%d) supported, using double buffering\n", major, minor);
	int num_screens = 1;
	auto info = XdbeGetVisualInfo(display, &DefaultRootWindow(display), &num_screens);
	if (!info || num_screens < 1 || info->count < 1) {
		fprintf(stderr, "No visuals support Xdbe\n");
		return {};
	}

	XVisualInfo visual_info = { .visualid = info->visinfo[0].visual, .screen = 0, .depth = info->visinfo[0].depth };

	XdbeFreeVisualInfo(info);

	int matches;
	auto match = XGetVisualInfo(display, VisualIDMask | VisualScreenMask | VisualDepthMask, &visual_info, &matches);
	if (!match || matches < 1) {
		fprintf(stderr, "Couldn't match a Visual with double buffering\n");
		return {};
	}

	return match->visual;
}

int main()
{
	auto display = XOpenDisplay(0);
	auto visual = get_xdbe_visual(display);
	if (!visual.has_value())
		return 1;

	auto screen = DefaultScreen(display);

	auto black_color = BlackPixel(display, screen);
	auto white_color = WhitePixel(display, screen);

	XSetWindowAttributes attrs = { .background_pixel = white_color };
	auto window = XCreateWindow(display, DefaultRootWindow(display), 0, 0, 1280, 720, 0, CopyFromParent, CopyFromParent, visual.value(), CWBackPixel, &attrs);
	if (!window) {
		fprintf(stderr, "Failed to XCreateWindow\n");
		return 1;
	}

	XSetStandardProperties(display, window, "My game", "HI!", 0, 0, 0, 0);

	auto back_buffer = XdbeAllocateBackBufferName(display, window, XdbeBackground);

	XSelectInput(display, window, ExposureMask | ButtonPressMask | KeyPressMask);

	auto gc = XCreateGC(display, window, 0, 0);

	//XSetBackground(display, gc, white_color);
	//XSetForeground(display, gc, black_color);

	XClearWindow(display, window);
	XMapRaised(display, window);

	XEvent event;
	while (1) {
		XNextEvent(display, &event);
		if (event.type == ButtonPress) {
			printf("You pressed a button at (%i, %i)\n", event.xbutton.x, event.xbutton.y);
		}
	}

	XdbeDeallocateBackBufferName(display, back_buffer);

	printf("ola eu sou o edgar\n");
}
