#pragma once
#include "action.h"
#include "array.h"
#include <libinput.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

// clang-format off
/*                             A modifier is active when either of the keys that belong to it is pressed
 struct key_binding[MAX_KEYCODE]
    |      .....           |         struct modifier
    +----------------------+        +--------------+(n)
[21]| KEY_BINDING_MODIFIER |------->| +-entries    |<------+
    +----------------------+        +-|------------+       |
[22]| KEY_BINDING_ENTRIES  |---    (1)| Invalidates entries| Checks if all the associated modifiers are activated
    +----------------------+(1)\      | when inactivated   |
    |       .....          |    \  (n)v                    |
Indices correspond to keycodes   \   +----------------+    |
                                  \->|    modifiers---|----+
                                  (n)+----------------+(1)
                                      struct key_entry
*/
// clang-format on

#define MAX_KEYCODE 512

struct modifier;
struct key_entry;
ARRAY_DEF(struct modifier *, modifier_ptr);
ARRAY_DEF(struct modifier, modifier);
ARRAY_DEF(struct key_entry *, key_entry_ptr);

struct modifier {
    bool active;
    // necessary for invalidating all entries associated with this
    // modifier when this modifier is inactivated
    struct array_key_entry_ptr entries;
#ifdef DEBUG
    const char *_debug_name;
#endif
};

#define IDENTITY_TRANSFER UINT32_MAX

struct key_entry {
    struct array_modifier_ptr modifiers;
    struct action press_action;
    struct action release_action;
    bool active;
#ifdef DEBUG
    uint32_t _debug_keycode;
#endif
};

#define KEY_BINDING_NULL 0
#define KEY_BINDING_ENTRIES 1
#define KEY_BINDING_MODIFIER 2

struct key_binding {
    int type;
    union {
        // type == KEY_BINDING_ENTRIES
        struct array_key_entry_ptr entries;
        // type == KEY_BINDING_MODIFIER
        struct {
            struct modifier *modifier;
            // 0 when not sending this modifier's key signals, the keycode when
            // this modifier sends a custom key signal,
            // IDENTITY_TRANSFER(default) when this modifier sends its received
            // key signal.
            uint32_t transfer;
        };
    };
};

void key_consumer_handle(struct libinput_event *ev);

extern struct key_binding key_bindings[MAX_KEYCODE];
