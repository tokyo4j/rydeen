#include "rydeen.h"
#include "array.h"
#include "config.h"
#include "gesture_consumer.h"
#include "key_consumer.h"
#include "uinput.h"
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

struct libinput *li;
struct ev_loop *loop;

static bool is_keyboard(struct libevdev *evdev) {
    static uint32_t test_keycodes[] = {KEY_A, KEY_0, KEY_MUTE};

    if (!libevdev_has_event_type(evdev, EV_KEY))
        return false;
    for (int i = 0; i < (int)ARRAY_SIZE(test_keycodes); i++)
        if (libevdev_has_event_code(evdev, EV_KEY, test_keycodes[i]))
            return true;
    return false;
}

static int open_restricted(const char *path, int flags, void *user_data) {
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

static void close_restricted(int fd, void *user_data) { close(fd); }

static const struct libinput_interface interface = {
    .open_restricted = open_restricted,
    .close_restricted = close_restricted,
};

static void on_li_events_ready(EV_P_ ev_io *w, int revents) {
    if (libinput_dispatch(li) < 0)
        return;

    struct libinput_event *ev;
    while ((ev = libinput_get_event(li))) {
        enum libinput_event_type event_type = libinput_event_get_type(ev);
        switch (event_type) {
        case LIBINPUT_EVENT_KEYBOARD_KEY:
        case LIBINPUT_EVENT_POINTER_BUTTON:
            key_consumer_handle(ev);
            break;
        case LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN:
        case LIBINPUT_EVENT_GESTURE_SWIPE_UPDATE:
        case LIBINPUT_EVENT_GESTURE_SWIPE_END:
        case LIBINPUT_EVENT_GESTURE_PINCH_BEGIN:
        case LIBINPUT_EVENT_GESTURE_PINCH_UPDATE:
        case LIBINPUT_EVENT_GESTURE_PINCH_END:
            gesture_consumer_handle(ev);
            break;
        default:;
        }
    }
    ev_io_start(EV_A_ w);
}

int main() {
    loop = ev_default_loop(0);

    parse_config();
    uinput_init();

    struct udev *udev = udev_new();
    li = libinput_udev_create_context(&interface, NULL, udev);
    libinput_udev_assign_seat(li, "seat0");

    struct ev_io li_watcher = {.data = li};
    ev_io_init(&li_watcher, on_li_events_ready, libinput_get_fd(li), EV_READ);

    on_li_events_ready(EV_A_ & li_watcher, 0);
    ev_run(loop, 0);
    return 0;
}
