#pragma once

#include "util.h"
#include <ev.h>
#include <stdbool.h>
#include <stdint.h>
#include <tllist.h>

struct libinput;
struct libevdev;

struct modifier_key {
	uint32_t keycode;
	uint32_t send_keycode;
};

struct modifier {
	const char *name;
	// The modifier is activated when any of keys are pressed
	tll(struct modifier_key) keys;
	bool activated;
};

struct key_signal {
	bool press;
	uint32_t keycode;
};

typedef tll(struct key_signal) key_signals_t;

struct action {
	enum { ACTION_KEY, ACTION_COMMAND } type;
	union {
		// type == ACTION_KEY
		key_signals_t signals;
		// type == ACTION_COMMAND
		const char *cmd;
	};
};

struct keybind {
	uint32_t keycode;
	tll(struct modifier *) modifiers; // not owned
	struct action on_press;
	struct action on_release;
	bool active;
};

struct gesturebind {
	int nr_fingers;
	enum direction direction;
	bool repeat;
	struct action on_forward;
	struct action on_backward;
};

struct config {
	double swipe_thr;
	double key_interval;
	double key_repeat_delay;
	double key_repeat_interval;

	tll(struct modifier) modifiers;
	tll(struct keybind) keybinds;
	tll(struct gesturebind) gesturebinds;
};

struct swipe_state {
	double x, y;
	enum direction direction;
	bool active;
	bool reversing;
	int nr_fingers;
};

struct uinput {
	struct server *server;
	struct libevdev_uinput *keyboard, *mouse;
	struct ev_timer repeat_timer;
	uint32_t last_keycode;
};

struct server {
	struct ev_loop *loop;
	struct ev_io li_watcher;
	struct libinput *li;
	struct uinput uinput;
	struct config config;
	struct ryd_set pressed_keys;
	struct swipe_state swipe_state;
};

bool is_rydeen_device(struct libevdev *evdev);
void uinput_init(struct server *server);
void uinput_finish(struct server *server);
void uinput_send(struct server *server, uint32_t keycode, bool press,
		 bool repeat);

void action_run(struct server *server, struct action *action);

void config_init(struct server *server);
void config_finish(struct server *server);
