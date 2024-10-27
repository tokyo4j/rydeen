#include "rydeen.h"
#include <ev.h>
#include <libevdev/libevdev-uinput.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define RYDEEN_VENDOR_ID 0xcafe
#define RYDEEN_KEYBOARD_PRODUCT_ID 0x1234
#define RYDEEN_MOUSE_PRODUCT_ID 0x1235

static void
handle_key_repeat(struct ev_loop *loop, struct ev_timer *timer, int revents)
{
	struct server *server = timer->data;
	struct uinput *uinput = &server->uinput;
	libevdev_uinput_write_event(uinput->keyboard, EV_KEY,
				    uinput->last_keycode, 2);
	libevdev_uinput_write_event(uinput->keyboard, EV_SYN, SYN_REPORT, 0);
	ev_timer_again(loop, timer);
}

bool
is_rydeen_device(struct libevdev *evdev)
{
	int vendor_id = libevdev_get_id_vendor(evdev);
	int product_id = libevdev_get_id_product(evdev);
	return vendor_id == RYDEEN_VENDOR_ID
	       && (product_id == RYDEEN_KEYBOARD_PRODUCT_ID
		   || product_id == RYDEEN_MOUSE_PRODUCT_ID);
}

static struct libevdev_uinput *
create_virtual_keyboard(void)
{
	struct libevdev *dev = libevdev_new();
	libevdev_set_name(dev, "Rydeen virtual keyboard");
	libevdev_set_id_vendor(dev, RYDEEN_VENDOR_ID);
	libevdev_set_id_product(dev, RYDEEN_KEYBOARD_PRODUCT_ID);
	libevdev_enable_event_type(dev, EV_KEY);
	for (int i = KEY_ESC; i <= KEY_MICMUTE; i++)
		libevdev_enable_event_code(dev, EV_KEY, i, NULL);

	struct libevdev_uinput *virtual_keyboard;
	if (libevdev_uinput_create_from_device(
		    dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &virtual_keyboard)
	    < 0) {
		perror("Could create uinput device");
		exit(1);
	}

	libevdev_free(dev);
	return virtual_keyboard;
}

static struct libevdev_uinput *
create_virtual_mouse(void)
{
	struct libevdev *dev = libevdev_new();
	libevdev_set_name(dev, "Rydeen virtual mouse");
	libevdev_set_id_vendor(dev, RYDEEN_VENDOR_ID);
	libevdev_set_id_product(dev, RYDEEN_MOUSE_PRODUCT_ID);
	libevdev_enable_event_type(dev, EV_KEY);
	libevdev_enable_event_code(dev, EV_KEY, BTN_LEFT, NULL);
	libevdev_enable_event_code(dev, EV_KEY, BTN_RIGHT, NULL);
	libevdev_enable_event_code(dev, EV_KEY, BTN_MIDDLE, NULL);
	libevdev_enable_event_code(dev, EV_KEY, BTN_EXTRA, NULL);
	libevdev_enable_event_code(dev, EV_KEY, BTN_SIDE, NULL);
	libevdev_enable_event_type(dev, EV_REL);
	libevdev_enable_event_code(dev, EV_REL, REL_X, NULL);
	libevdev_enable_event_code(dev, EV_REL, REL_Y, NULL);
	libevdev_enable_event_code(dev, EV_REL, REL_WHEEL, NULL);

	struct libevdev_uinput *virtual_mouse;
	if (libevdev_uinput_create_from_device(
		    dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &virtual_mouse)
	    < 0) {
		perror("Could create uinput device");
		exit(1);
	}

	libevdev_free(dev);
	return virtual_mouse;
}

void
uinput_init(struct server *server)
{
	server->uinput.keyboard = create_virtual_keyboard();
	server->uinput.mouse = create_virtual_mouse();
	server->uinput.server = server;

	ev_init(&server->uinput.repeat_timer, handle_key_repeat);
	server->uinput.repeat_timer.data = server;
}

void
uinput_finish(struct server *server)
{
	libevdev_uinput_destroy(server->uinput.keyboard);
	server->uinput.keyboard = NULL;
	libevdev_uinput_destroy(server->uinput.mouse);
	server->uinput.mouse = NULL;
}

void
uinput_send(struct server *server, uint32_t keycode, bool press, bool repeat)
{
	struct ev_loop *loop = server->loop;
	struct config *config = &server->config;
	struct uinput *uinput = &server->uinput;

	if (keycode < 256) {
		libevdev_uinput_write_event(uinput->keyboard, EV_KEY, keycode,
					    press);
		libevdev_uinput_write_event(uinput->keyboard, EV_SYN,
					    SYN_REPORT, 0);
		if (repeat) {
			if (press) {
				if (uinput->last_keycode)
					ev_timer_stop(loop,
						      &uinput->repeat_timer);
				uinput->last_keycode = keycode;
				ev_timer_set(&uinput->repeat_timer,
					     config->key_repeat_delay,
					     config->key_repeat_interval);
				ev_timer_start(loop, &uinput->repeat_timer);
			} else {
				if (keycode == uinput->last_keycode) {
					uinput->last_keycode = 0;
					ev_timer_stop(loop,
						      &uinput->repeat_timer);
				}
			}
		}
	} else {
		libevdev_uinput_write_event(uinput->mouse, EV_KEY, keycode,
					    press);
		libevdev_uinput_write_event(uinput->mouse, EV_SYN, SYN_REPORT,
					    0);
	}
}
