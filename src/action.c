#include "action.h"
#include "array.h"
#include "config.h"
#include "rydeen.h"
#include "uinput.h"
#include <ev.h>
#include <stdio.h>
#include <unistd.h>
#include <wait.h>

struct key_signal_context {
    struct array_key_signal *signals;
    size_t i;
};

static bool context_done(struct key_signal_context *ctx) {
    return ctx->i >= ctx->signals->size;
}

static inline void send_key_signal(struct key_signal_context *ctx) {
    struct key_signal *ks = &ctx->signals->elems[ctx->i++];
    uinput_send(ks->keycode, ks->state, true);
}

static void do_key_action_callback(EV_P_ ev_timer *timer, int revents) {
    struct key_signal_context *ctx = timer->data;
    send_key_signal(ctx);
    if ((context_done(ctx))) {
        ev_timer_stop(EV_A_ timer);
        free(ctx);
        free(timer);
    } else {
        ev_timer_again(EV_A_ timer);
    }
}

static void do_key_action(struct array_key_signal *signals) {
    struct key_signal_context *ctx = malloc(sizeof(*ctx));
    ctx->i = 0;
    ctx->signals = signals;

    if (config_key_interval == 0.) {
        while (!context_done(ctx))
            send_key_signal(ctx);
    } else {
        send_key_signal(ctx);
        if (!context_done(ctx)) {
            struct ev_timer *timer = malloc(sizeof(*timer));
            timer->data = ctx;
            ev_timer_init(timer, do_key_action_callback, 0.,
                          config_key_interval);
            ev_timer_again(EV_A_ timer);
        }
    }
}

static void child_callback(EV_P_ struct ev_child *child_watcher, int revents) {
    int status;
    waitpid(child_watcher->rpid, &status, 0);
    if (!WIFEXITED(status))
        fprintf(stderr, "The child process has not been terminated\n");
    ev_child_stop(EV_A_ child_watcher);
    free(child_watcher);
}

static void do_cmd_action(const char *cmd) {
    int pid = fork();

    if (pid == 0) {
        debug("Executing command: %s\n", cmd);
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        execlp(config_shell, config_shell, "-c", cmd, NULL);
        fprintf(stderr, "Could not run command: %s\n", cmd);
        exit(1);
    } else {
        struct ev_child *child_watcher = malloc(sizeof(*child_watcher));
        ev_child_init(child_watcher, child_callback, pid, 0);
        ev_child_start(EV_A_ child_watcher);
    }
}

void do_action(struct action *action) {
    switch (action->type) {
    case ACTION_KEY:
        do_key_action(&action->key_signals);
        break;
    case ACTION_CMD:
        do_cmd_action(action->cmd);
        break;
    default:
        break;
    }
}
