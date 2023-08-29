#include "gesture_consumer.h"
#include "config.h"
#include <libinput.h>
#include <stdbool.h>
#include <stdio.h>

struct gesture_binding gesture_bindings[32];

struct swipe_state {
    double x, y;
    enum direction dir;
    bool active;
    bool reversing;
    int fingers;
};

static struct swipe_state swipe_state;

void gesture_consumer_handle(struct libinput_event *ev) {
    enum libinput_event_type ev_type = libinput_event_get_type(ev);
    struct libinput_event_gesture *gev = libinput_event_get_gesture_event(ev);
    switch (ev_type) {
    case LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN:
        swipe_state.fingers = libinput_event_gesture_get_finger_count(gev);
        break;
    case LIBINPUT_EVENT_GESTURE_SWIPE_UPDATE: {
        swipe_state.x += libinput_event_gesture_get_dx(gev);
        swipe_state.y += libinput_event_gesture_get_dy(gev);
        enum direction ev_dir = 0;
        if (swipe_state.x > config_swipe_thr)
            ev_dir = RIGHT;
        else if (swipe_state.x < -config_swipe_thr)
            ev_dir = LEFT;
        else if (swipe_state.y > config_swipe_thr)
            ev_dir = DOWN;
        else if (swipe_state.y < -config_swipe_thr)
            ev_dir = UP;
        else
            return;

        swipe_state.x = swipe_state.y = 0;

        bool repeating;
        bool reversing;

        if (!swipe_state.active) {
            swipe_state.active = true;
            swipe_state.dir = ev_dir;
            repeating = false;
            reversing = false;
        } else if (swipe_state.dir == ev_dir) {
            reversing = false;
            if (swipe_state.reversing) {
                swipe_state.reversing = false;
                repeating = false;
            } else {
                repeating = true;
            }
        } else if (swipe_state.dir == OPPOSITE(ev_dir)) {
            reversing = true;
            if (!swipe_state.reversing) {
                swipe_state.reversing = true;
                repeating = false;
            } else {
                repeating = true;
            }
        } else {
            return;
        }

        struct gesture_binding *binding = GET_GESTURE_BINDING(
            swipe_state.dir, GESTURE_BINDING_SWIPE, swipe_state.fingers);
        if (reversing) {
            if (!(repeating && !binding->repeats))
                do_action(&binding->backward_action);
        } else {
            if (!(repeating && !binding->repeats))
                do_action(&binding->forward_action);
        }
        break;
    }
    case LIBINPUT_EVENT_GESTURE_SWIPE_END:
        swipe_state.active = false;
        swipe_state.x = swipe_state.y = 0;
        break;
    default:
        break;
    }
}
