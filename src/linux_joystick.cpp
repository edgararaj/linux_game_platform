#include <dlfcn.h>
#include <fcntl.h>
#include <linux/joystick.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ipc.h>
#include <unistd.h>

#include "types.h"

#define MAX_EVENTS 1024
#define LEN_NAME 16
#define EVENT_SIZE sizeof(inotify_event)
#define BUF_LEN (MAX_EVENTS * (EVENT_SIZE + LEN_NAME))

#define JOYSTICK_DIR "/dev/input"

struct Joystick {
	int range_min;
	int range_max;
	int fd;
};

struct JoystickInotify {
	int fd, wd;
};

void joystick_inotify_setup(JoystickInotify& inotify)
{
	inotify.fd = inotify_init();
	if (fcntl(inotify.fd, F_SETFL, O_NONBLOCK) < 0) {
		fprintf(stderr, "Failed to fctnl\n");
	}

	auto path = JOYSTICK_DIR;
	inotify.wd = inotify_add_watch(inotify.fd, path, IN_CREATE | IN_DELETE);
	if (inotify.wd != -1)
		printf("[INOTIFY]: Watching %s\n", path);
}

void joystick_inotify_close(const JoystickInotify& inotify)
{
	inotify_rm_watch(inotify.fd, inotify.wd);
	close(inotify.fd);
}

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

bool get_joystick_by_index(Joystick& joy, const int i)
{
	char joy_path[sizeof(JOYSTICK_DIR "/js1")]; // @Volatile_max_joy_count
	snprintf(joy_path, sizeof(joy_path), JOYSTICK_DIR "/js%i", i);

	if (get_joystick(joy, joy_path))
		return true;

	return false;
}

void get_joysticks(Joystick* const joysticks, const int max_joy_count)
{
	for (int i = 0; i < max_joy_count; i++) {
		auto& joy = joysticks[i];
		get_joystick_by_index(joy, i);
	}
}

void joystick_inotify_update(const JoystickInotify& inotify, Joystick* const joysticks, const int max_joy_count)
{
	char ibuffer[BUF_LEN];

	auto length = read(inotify.fd, ibuffer, BUF_LEN);

	for (int i = 0; i < length;) {

		auto event = (inotify_event*)&ibuffer[i];

		if (event->len) {
			if ((event->mask & IN_ISDIR) == 0 && event->mask & (IN_CREATE | IN_DELETE)) {
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
}
