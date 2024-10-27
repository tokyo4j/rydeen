#include "rydeen.h"
#include <ev.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <wait.h>

typedef __typeof__((key_signals_t){0}.head) key_signal_it_t;

struct key_action_context {
	struct server *server;
	key_signals_t *signals;
	key_signal_it_t signal_it;
	ev_timer timer;
};

static inline void
send_key_signal(struct key_action_context *ctx)
{
	struct key_signal *signal = &ctx->signal_it->item;
	uinput_send(ctx->server, signal->keycode, signal->press, true);
	ctx->signal_it = ctx->signal_it->next;
}

static void
handle_key_action_timeout(struct ev_loop *loop, ev_timer *timer, int revents)
{
	struct key_action_context *ctx = timer->data;
	send_key_signal(ctx);
	if (!ctx->signal_it) {
		ev_timer_stop(loop, timer);
		free(ctx);
	} else {
		ev_timer_again(loop, timer);
	}
}

static void
run_key_action(struct server *server, key_signals_t *signals)
{
	struct ev_loop *loop = server->loop;
	struct config *config = &server->config;

	struct key_action_context *ctx = znew(*ctx);
	ctx->server = server;
	ctx->signals = signals;
	ctx->signal_it = signals->head;

	if (config->key_interval == 0.) {
		while (ctx->signal_it)
			send_key_signal(ctx);
		free(ctx);
	} else {
		send_key_signal(ctx);
		if (ctx->signal_it) {
			ctx->timer.data = ctx;
			ev_timer_init(&ctx->timer, handle_key_action_timeout,
				      0., config->key_interval);
			ev_timer_again(loop, &ctx->timer);
		} else {
			free(ctx);
		}
	}
}

static void
handle_process_exit(struct ev_loop *loop, struct ev_child *child_watcher,
		    int revents)
{
	int status;
	waitpid(child_watcher->rpid, &status, 0);
	if (!WIFEXITED(status))
		fprintf(stderr, "The child process has not been terminated\n");
	ev_child_stop(loop, child_watcher);
	free(child_watcher);
}

static void
run_command_action(struct server *server, const char *cmd)
{
	struct ev_loop *loop = server->loop;

	pid_t pid = fork();
	if (pid == 0) {
		debug("Executing command: %s\n", cmd);
		close(STDIN_FILENO);
		close(STDOUT_FILENO);
		execlp("/bin/sh", "/bin/sh", "-c", cmd, NULL);
		fprintf(stderr, "Could not run command: %s\n", cmd);
		exit(1);
	} else {
		struct ev_child *child_watcher = znew(*child_watcher);
		ev_child_init(child_watcher, handle_process_exit, pid, 0);
		ev_child_start(loop, child_watcher);
	}
}

void
action_run(struct server *server, struct action *action)
{
	switch (action->type) {
	case ACTION_KEY:
		run_key_action(server, &action->signals);
		break;
	case ACTION_COMMAND:
		run_command_action(server, action->cmd);
		break;
	default:
		break;
	}
}
