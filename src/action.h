#pragma once
#include "array.h"
#include <stdbool.h>

struct key_signal;
ARRAY_DEF(struct key_signal, key_signal);

struct key_signal {
    uint32_t keycode;
    bool state;
};

#define ACTION_NULL 0
#define ACTION_CMD 1
#define ACTION_KEY 2

struct action {
    int type;
    union {
        const char *cmd;
        struct array_key_signal key_signals;
    };
};

void do_action(struct action *action);
