#include "key_consumer.h"
#include "action.h"
#include "array.h"
#include "rydeen.h"
#include "uinput.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

struct key_binding key_bindings[MAX_KEYCODE];

static inline bool is_modifiers_active(struct array_modifier_ptr *modifiers) {
    array_foreach(modifiers, modifier) {
        if (!modifier->active)
            return false;
    }
    return true;
}

void key_consumer_handle(struct libinput_event *ev) {
    enum libinput_event_type event_type = libinput_event_get_type(ev);
    uint32_t keycode;
    int state;
    if (event_type == LIBINPUT_EVENT_KEYBOARD_KEY) {
        struct libinput_event_keyboard *kev =
            libinput_event_get_keyboard_event(ev);
        keycode = libinput_event_keyboard_get_key(kev);
        state = libinput_event_keyboard_get_key_state(kev);
    } else if (event_type == LIBINPUT_EVENT_POINTER_BUTTON) {
        struct libinput_event_pointer *pev =
            libinput_event_get_pointer_event(ev);
        keycode = libinput_event_pointer_get_button(pev);
        state = libinput_event_pointer_get_button_state(pev);
    } else
        return;

    debug("%d, %d from %s\n", keycode, state,
          libinput_device_get_name(libinput_event_get_device(ev)));

    struct key_binding *binding = &key_bindings[keycode];
    switch (binding->type) {
    case KEY_BINDING_NULL:
        if (keycode < 256)
            uinput_send(keycode, state, true);
        return;
    case KEY_BINDING_MODIFIER: {
        struct modifier *modifier = binding->modifier;
        if (binding->transfer) {
            if (binding->transfer == IDENTITY_TRANSFER)
                uinput_send(keycode, state, false);
            else
                uinput_send(binding->transfer, state, false);
        }
        modifier->active = state;
        // if the modifier key is released, perform release actions of all
        // associated entries.
        if (state == 0) {
            array_foreach(&modifier->entries, entry_ptr) {
                struct key_entry *entry = entry_ptr;
                if (entry->active) {
                    entry->active = false;
                    do_action(&entry->release_action);
                }
            }
        }
        return;
    }
    case KEY_BINDING_ENTRIES: {
        struct array_key_entry_ptr *entries = &binding->entries;
        array_foreach_reverse(entries, entry) {
            if (entry->active && state == 0) {
                entry->active = false;
                do_action(&entry->release_action);
                return;
            } else if (!entry->active && state == 1 &&
                       is_modifiers_active(&entry->modifiers)) {
                entry->active = true;
                do_action(&entry->press_action);
                return;
            }
        }
        return;
    }
    }
}
