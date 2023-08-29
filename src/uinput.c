#include "uinput.h"
#include "config.h"
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

struct libevdev_uinput *uidev_key, *uidev_mouse;
struct ev_timer repeat_timer;
uint32_t last_keycode;

static void repeat_timer_callback(EV_P_ struct ev_timer *timer, int revents) {
    libevdev_uinput_write_event(uidev_key, EV_KEY, last_keycode, 2);
    libevdev_uinput_write_event(uidev_key, EV_SYN, SYN_REPORT, 0);
    ev_timer_again(EV_A_ & repeat_timer);
}

bool is_rydeen_device(struct libevdev *evdev) {
    int vendor_id = libevdev_get_id_vendor(evdev);
    int product_id = libevdev_get_id_product(evdev);
    return vendor_id == RYDEEN_VENDOR_ID &&
           (product_id == RYDEEN_KEYBOARD_PRODUCT_ID ||
            product_id == RYDEEN_MOUSE_PRODUCT_ID);
}

void uinput_init() {
    {
        struct libevdev *dev = libevdev_new();
        libevdev_set_name(dev, "Rydeen virtual keyboard");
        libevdev_set_id_vendor(dev, RYDEEN_VENDOR_ID);
        libevdev_set_id_product(dev, RYDEEN_KEYBOARD_PRODUCT_ID);
        libevdev_enable_event_type(dev, EV_KEY);
        for (int i = KEY_ESC; i <= KEY_MICMUTE; i++)
            libevdev_enable_event_code(dev, EV_KEY, i, NULL);
        if (libevdev_uinput_create_from_device(
                dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &uidev_key) < 0) {
            perror("Could create uinput device");
            exit(1);
        }
        libevdev_free(dev);
    }
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

        if (libevdev_uinput_create_from_device(
                dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &uidev_mouse) < 0) {
            perror("Could create uinput device");
            exit(1);
        }
        libevdev_free(dev);
    }

    ev_init(&repeat_timer, repeat_timer_callback);
}

void uinput_send(uint32_t keycode, int state, bool repeats) {
    if (keycode < 256) {
        libevdev_uinput_write_event(uidev_key, EV_KEY, keycode, state);
        libevdev_uinput_write_event(uidev_key, EV_SYN, SYN_REPORT, 0);
        if (repeats) {
            if (state == 1) {
                if (last_keycode)
                    ev_timer_stop(EV_A_ & repeat_timer);
                last_keycode = keycode;
                ev_timer_set(&repeat_timer, config_key_repeat_delay,
                             config_key_repeat_interval);
                ev_timer_start(EV_A_ & repeat_timer);
            } else {
                if (keycode == last_keycode) {
                    last_keycode = 0;
                    ev_timer_stop(EV_A_ & repeat_timer);
                }
            }
        }
    } else {
        libevdev_uinput_write_event(uidev_mouse, EV_KEY, keycode, state);
        libevdev_uinput_write_event(uidev_mouse, EV_SYN, SYN_REPORT, 0);
    }
}