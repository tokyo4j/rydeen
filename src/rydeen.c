#include "rydeen.h"
#include <assert.h>
#include <errno.h>
#include <ev.h>
#include <fcntl.h>
#include <libevdev/libevdev-uinput.h>
#include <libevdev/libevdev.h>
#include <libinput.h>
#include <libudev.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static bool
is_keyboard(struct libevdev *evdev)
{
	static uint32_t test_keycodes[] = {KEY_A, KEY_0, KEY_MUTE};

	if (libevdev_has_event_type(evdev, EV_REL))
		return false;
	if (!libevdev_has_event_type(evdev, EV_KEY))
		return false;
	for (int i = 0; i < (int)ARRAY_SIZE(test_keycodes); i++)
		if (libevdev_has_event_code(evdev, EV_KEY, test_keycodes[i]))
			return true;
	return false;
}

static int
open_restricted(const char *path, int flags, void *user_data)
{
	int fd = open(path, flags);
	if (fd < 0)
		return -errno;
	struct libevdev *evdev;
	if (libevdev_new_from_fd(fd, &evdev) < 0)
		return fd;

	debug("Found device: %s", libevdev_get_name(evdev));

	if (is_rydeen_device(evdev)) {
		debug(" - ignored\n");
		libevdev_free(evdev);
		close(fd);
		return -1;
	}

	if (is_keyboard(evdev)) {
		debug(" - grabbed\n");
		libevdev_grab(evdev, LIBEVDEV_GRAB);
	} else {
		debug(" - not grabbed\n");
	}

	libevdev_free(evdev);

	return fd;
}

static void
close_restricted(int fd, void *user_data)
{
	close(fd);
}

static const struct libinput_interface interface = {
	.open_restricted = open_restricted,
	.close_restricted = close_restricted,
};

static bool
handle_modifier_key(struct server *server, struct modifier *modifier,
		    uint32_t keycode, bool pressed)
{
	bool handled = false;
	bool activate = false;

	tll_foreach(modifier->keys, it) {
		if (ryd_set_contains(&server->pressed_keys, it->item.keycode))
			activate = true;
		if (keycode == it->item.keycode) {
			assert(!handled);
			handled = true;
			if (it->item.send_keycode)
				uinput_send(server, it->item.send_keycode,
					    pressed, false);
		}
	}

	// When a modifier is deactivated, deactivate all the keybinds
	// associated with it
	if (modifier->activated && !activate) {
		tll_foreach(server->config.keybinds, bind_it) {
			bool deactivate_keybind = false;
			tll_foreach(bind_it->item.modifiers, mod_it) {
				if (mod_it->item == modifier) {
					deactivate_keybind = true;
					break;
				}
			}
			if (deactivate_keybind && bind_it->item.active) {
				bind_it->item.active = false;
				action_run(server, &bind_it->item.on_release);
			}
		}
	}

	modifier->activated = activate;

	return handled;
}

static bool
handle_keybind_key(struct server *server, struct keybind *keybind,
		   uint32_t keycode, bool pressed)
{
	if (keybind->keycode != keycode)
		return false;
	if (keybind->active == pressed)
		return false;

	tll_foreach(keybind->modifiers, it) {
		if (!it->item->activated)
			return false;
	}

	keybind->active = pressed;
	action_run(server, pressed ? &keybind->on_press : &keybind->on_release);
	return true;
}

static void
handle_key_event(struct server *server, enum libinput_event_type event_type,
		 struct libinput_event *event)
{
	struct config *config = &server->config;
	uint32_t keycode;
	bool pressed;
	if (event_type == LIBINPUT_EVENT_KEYBOARD_KEY) {
		struct libinput_event_keyboard *kev =
			libinput_event_get_keyboard_event(event);
		keycode = libinput_event_keyboard_get_key(kev);
		pressed = libinput_event_keyboard_get_key_state(kev)
			  == LIBINPUT_KEY_STATE_PRESSED;
	} else if (event_type == LIBINPUT_EVENT_POINTER_BUTTON) {
		struct libinput_event_pointer *pev =
			libinput_event_get_pointer_event(event);
		keycode = libinput_event_pointer_get_button(pev);
		pressed = libinput_event_pointer_get_button_state(pev)
			  == LIBINPUT_BUTTON_STATE_PRESSED;
	} else {
		return;
	}

	if (pressed)
		ryd_set_add(&server->pressed_keys, keycode);
	else
		ryd_set_remove(&server->pressed_keys, keycode);

	tll_foreach(config->modifiers, it) {
		if (handle_modifier_key(server, &it->item, keycode, pressed))
			return;
	}

	bool handled = false;
	tll_foreach(config->keybinds, it) {
		handled |=
			handle_keybind_key(server, &it->item, keycode, pressed);
	}
	if (!handled)
		uinput_send(server, keycode, pressed, true);
}

static void
handle_gesture_event(struct server *server, enum libinput_event_type event_type,
		     struct libinput_event *event)
{
	struct swipe_state *state = &server->swipe_state;
	struct config *config = &server->config;

	struct libinput_event_gesture *gesture_event =
		libinput_event_get_gesture_event(event);
	switch (event_type) {
	case LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN:
		state->x = 0;
		state->y = 0;
		state->nr_fingers =
			libinput_event_gesture_get_finger_count(gesture_event);
		state->direction = DIRECTION_NONE;
		break;
	case LIBINPUT_EVENT_GESTURE_SWIPE_UPDATE: {
		state->x += libinput_event_gesture_get_dx(gesture_event);
		state->y += libinput_event_gesture_get_dy(gesture_event);
		enum direction ev_dir = 0;
		if (state->x > config->swipe_thr) {
			ev_dir = DIRECTION_RIGHT;
			state->x -= config->swipe_thr;
		} else if (state->x < -config->swipe_thr) {
			ev_dir = DIRECTION_LEFT;
			state->x += config->swipe_thr;
		} else if (state->y > config->swipe_thr) {
			ev_dir = DIRECTION_DOWN;
			state->y -= config->swipe_thr;
		} else if (state->y < -config->swipe_thr) {
			ev_dir = DIRECTION_UP;
			state->y += config->swipe_thr;
		} else {
			break;
		}

		bool repeating;
		if (state->direction == DIRECTION_NONE) {
			state->direction = ev_dir;
			repeating = false;
		} else if (ev_dir == state->direction
			   || ev_dir == direction_opposite(state->direction)) {
			repeating = true;
		} else {
			break;
		}

		tll_foreach(config->gesturebinds, it) {
			if (!it->item.repeat && repeating)
				continue;
			if (state->direction != it->item.direction
			    || state->nr_fingers != it->item.nr_fingers)
				continue;
			if (ev_dir == state->direction)
				action_run(server, &it->item.on_forward);
			else
				action_run(server, &it->item.on_backward);
		}
		break;
	}
	default:
		break;
	}
}

static void
on_li_events_ready(struct ev_loop *loop, ev_io *w, int revents)
{
	struct server *server = w->data;
	struct libinput *li = server->li;

	if (libinput_dispatch(li) < 0)
		return;

	struct libinput_event *event;
	while ((event = libinput_get_event(li))) {
		enum libinput_event_type event_type =
			libinput_event_get_type(event);
		switch (event_type) {
		case LIBINPUT_EVENT_KEYBOARD_KEY:
			handle_key_event(server, event_type, event);
			break;
		case LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN:
		case LIBINPUT_EVENT_GESTURE_SWIPE_UPDATE:
		case LIBINPUT_EVENT_GESTURE_SWIPE_END:
			handle_gesture_event(server, event_type, event);
			break;
		default:
			break;
		}
		libinput_event_destroy(event);
	}
	ev_io_start(loop, w);
}

int
main(void)
{
	struct server server = {0};
	server.loop = ev_default_loop(0);

	config_init(&server);
	uinput_init(&server);

	struct udev *udev = udev_new();
	server.li = libinput_udev_create_context(&interface, NULL, udev);
	udev_unref(udev);
	libinput_udev_assign_seat(server.li, "seat0");

	server.li_watcher.data = &server;
	ev_io_init(&server.li_watcher, on_li_events_ready,
		   libinput_get_fd(server.li), EV_READ);
	on_li_events_ready(server.loop, &server.li_watcher, 0);

	ev_run(server.loop, 0);

	libinput_unref(server.li);
	uinput_finish(&server);
	config_finish(&server);

	return 0;
}
