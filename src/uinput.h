#pragma once

#include <libevdev/libevdev-uinput.h>
#include <stdbool.h>
#include <stdint.h>

bool is_rydeen_device(struct libevdev *evdev);
void uinput_init();
void uinput_send(uint32_t keycode, int state, bool repeats);
