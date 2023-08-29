#pragma once
#include "action.h"
#include "array.h"
#include <libinput.h>

enum direction {
    UP,
    RIGHT,
    DOWN,
    LEFT,
};
#define OPPOSITE(dir) ((enum direction)(((dir) + 2) % 4))

struct gesture_binding {
    bool repeats;
    struct action forward_action, backward_action;
};

#define GESTURE_BINDING_NULL 0
#define GESTURE_BINDING_SWIPE 1
#define GESTURE_BINDING_PINCH 2

// for each direction, null/swipe/pinch, 3/4 fingers (4*4*2=32)
extern struct gesture_binding gesture_bindings[32];
#define GET_GESTURE_BINDING(dir, type, fingers) \
    (&gesture_bindings[((int)(dir) << 3) + ((int)(type) << 1) + \
                       ((int)(fingers)-3)])
// for debugging
#define GET_GESTURE_DIRECTION(idx) ((enum direction)((idx) >> 3))
#define GET_GESTURE_TYPE(idx) (((idx) >> 1) & 3)
#define GET_GESTURE_FINGERS(idx) (((idx)&1) + 3)

void gesture_consumer_handle(struct libinput_event *ev);
