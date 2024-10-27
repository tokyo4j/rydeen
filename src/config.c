#include "config.h"
#include "rydeen.h"
#include <limits.h>
#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <string.h>
#include <xkbcommon/xkbcommon.h>
#include <yaml.h>

struct parser_context {
	yaml_parser_t parser;
	yaml_document_t doc;
	struct xkb_rule_names keyboard;
	struct xkb_context *xkb_ctx;
	struct xkb_keymap *keymap;
	struct config *config;
};

#define MAX_KEYCODE 512

#define PANIC(node) \
	do { \
		fprintf(stderr, "Falied to parse config at %ld:%ld\n", \
			(node)->start_mark.line + 1, \
			(node)->start_mark.column + 1); \
		abort(); \
	} while (0)

static yaml_node_t *
get_node_by_key(struct parser_context *ctx, const yaml_node_t *mapping_node,
		const char *key)
{
	if (mapping_node->type != YAML_MAPPING_NODE)
		return NULL;
	for (yaml_node_pair_t *pair = mapping_node->data.mapping.pairs.start;
	     pair < mapping_node->data.mapping.pairs.top; pair++) {
		yaml_node_t *key_node =
			yaml_document_get_node(&ctx->doc, pair->key);
		if (!strcmp((char *)key_node->data.scalar.value, key))
			return yaml_document_get_node(&ctx->doc, pair->value);
	}
	return NULL;
}

static inline double
node_to_double(yaml_node_t *node)
{
	if (node->type != YAML_SCALAR_NODE)
		PANIC(node);

	char *cast_err;
	double result = strtod((char *)node->data.scalar.value, &cast_err);
	if (*cast_err)
		PANIC(node);
	return result;
}

static inline int
node_to_int(yaml_node_t *node)
{
	if (node->type != YAML_SCALAR_NODE)
		PANIC(node);

	char *cast_err;
	long result = strtol((char *)node->data.scalar.value, &cast_err, 10);
	if (*cast_err || result > INT_MAX || result < INT_MIN)
		PANIC(node);
	return (int)result;
}

static inline const char *
node_to_str(yaml_node_t *node)
{
	if (node->type != YAML_SCALAR_NODE)
		PANIC(node);
	return (const char *)((node)->data.scalar.value);
}

static inline bool
node_to_bool(yaml_node_t *node)
{
	if (node->type != YAML_SCALAR_NODE)
		PANIC(node);
	char *str = (char *)node->data.scalar.value;
	if (!strcmp(str, "true"))
		return true;
	else if (!strcmp(str, "false"))
		return false;
	else
		PANIC(node);
}

struct keycode_keyname_pair {
	uint32_t keycode;
	const char *keyname;
} mouse_keysyms[] = {
	{.keycode = BTN_LEFT, .keyname = "mouse:left"},
	{.keycode = BTN_RIGHT, .keyname = "mouse:right"},
	{.keycode = BTN_MIDDLE, .keyname = "mouse:middle"},
	{.keycode = BTN_EXTRA, .keyname = "mouse:forward"},
	{.keycode = BTN_SIDE, .keyname = "mouse:backward"},
};

static uint32_t
keyname_to_keycode(struct parser_context *ctx, const char *keyname)
{
	for (int i = 0; i < ARRAY_SIZE(mouse_keysyms); i++)
		if (!strcmp(keyname, mouse_keysyms[i].keyname))
			return mouse_keysyms[i].keycode;

	for (uint32_t i = 0; i < MAX_KEYCODE; i++) {
		const xkb_keysym_t *syms;
		int syms_len = xkb_keymap_key_get_syms_by_level(
			ctx->keymap, i + 8, 0, 0, &syms);
		if (!syms_len)
			continue;
		for (int j = 0; j < syms_len; j++) {
			char sym_name[64];
			if (!xkb_keysym_get_name(syms[j], sym_name,
						 sizeof(sym_name)))
				continue;
			if (!strcmp(sym_name, keyname))
				return i;
		}
	}
	return 0;
}

static const char *
keycode_to_keyname(struct parser_context *ctx, uint32_t keycode)
{
	for (int i = 0; i < ARRAY_SIZE(mouse_keysyms); i++)
		if (keycode == mouse_keysyms[i].keycode)
			return mouse_keysyms[i].keyname;

	const xkb_keysym_t *syms;
	int syms_len = xkb_keymap_key_get_syms_by_level(
		ctx->keymap, keycode + 8, 0, 0, &syms);
	if (!syms_len)
		return NULL;

	static char buf[256];
	if (!xkb_keysym_get_name(syms[0], buf, sizeof(buf)))
		return NULL;

	return buf;
}

static void
parse_general(struct parser_context *ctx, const yaml_node_t *general_node)
{
	struct config *config = ctx->config;

	// "general.key_interval"
	yaml_node_t *key_interval_node =
		get_node_by_key(ctx, general_node, "key_interval");
	if (key_interval_node) {
		config->key_interval = node_to_double(key_interval_node);
	}

	// "general.key_repeat_delay"
	yaml_node_t *key_repeat_delay_node =
		get_node_by_key(ctx, general_node, "key_repeat_delay");
	if (key_repeat_delay_node) {
		config->key_repeat_delay =
			node_to_double(key_repeat_delay_node);
	}

	// "general.key_repeat_interval"
	yaml_node_t *key_repeat_interval_node =
		get_node_by_key(ctx, general_node, "key_repeat_interval");
	if (key_repeat_interval_node) {
		config->key_repeat_interval =
			node_to_double(key_repeat_interval_node);
	}

	// "general.swipe_threshold"
	yaml_node_t *swipe_thr_node =
		get_node_by_key(ctx, general_node, "swipe_threshold");
	if (swipe_thr_node) {
		config->swipe_thr = node_to_double(swipe_thr_node);
	}

	// "general.keyboard"
	yaml_node_t *keyboard_node =
		get_node_by_key(ctx, general_node, "keyboard");
	if (keyboard_node) {
		// "general.keyboard.rules"
		yaml_node_t *rules_node =
			get_node_by_key(ctx, keyboard_node, "rules");
		if (rules_node)
			ctx->keyboard.rules = node_to_str(rules_node);
		// "general.keyboard.model"
		yaml_node_t *model_node =
			get_node_by_key(ctx, keyboard_node, "model");
		if (model_node)
			ctx->keyboard.model = node_to_str(model_node);
		// "general.keyboard.layout"
		yaml_node_t *layout_node =
			get_node_by_key(ctx, keyboard_node, "layout");
		if (layout_node)
			ctx->keyboard.layout = node_to_str(layout_node);
		// "general.keyboard.variant"
		yaml_node_t *variant_node =
			get_node_by_key(ctx, keyboard_node, "variant");
		if (variant_node)
			ctx->keyboard.variant = node_to_str(variant_node);
		// "general.keyboard.options"
		yaml_node_t *options_node =
			get_node_by_key(ctx, keyboard_node, "options");
		if (options_node)
			ctx->keyboard.options = node_to_str(options_node);
	}
}

static void
parse_modifier(struct parser_context *ctx, const yaml_node_pair_t *modifier_kv)
{
	struct modifier modifier = {0};

	modifier.name = strdup(node_to_str(
		yaml_document_get_node(&ctx->doc, modifier_kv->key)));

	// "modifiers.(modifier_name)"
	yaml_node_t *modifier_val_node =
		yaml_document_get_node(&ctx->doc, modifier_kv->value);
	if (modifier_val_node->type != YAML_SEQUENCE_NODE)
		PANIC(modifier_val_node);

	for (yaml_node_item_t *item =
		     modifier_val_node->data.sequence.items.start;
	     item < modifier_val_node->data.sequence.items.top; item++) {
		// "modifiers.(modifier_name)[*]"
		yaml_node_t *modifier_item_node =
			yaml_document_get_node(&ctx->doc, *item);
		if (modifier_item_node->type != YAML_MAPPING_NODE)
			PANIC(modifier_item_node);

		// "modifiers.(modifier_name)[*].key"
		yaml_node_t *key_node =
			get_node_by_key(ctx, modifier_item_node, "key");
		const char *key_name = node_to_str(key_node);
		uint32_t keycode = keyname_to_keycode(ctx, key_name);
		if (!keycode)
			PANIC(key_node);

		struct modifier_key mod_key = {0};
		mod_key.keycode = keycode;

		// "modifiers.(modifier_name)[*].send_key"
		mod_key.send_keycode = keycode;
		yaml_node_t *send_key_node =
			get_node_by_key(ctx, modifier_item_node, "send_key");
		if (send_key_node) {
			const char *send_key_str = node_to_str(send_key_node);
			if (!strcmp(send_key_str, "false"))
				mod_key.send_keycode = 0;
			else {
				uint32_t send_key_keycode =
					keyname_to_keycode(ctx, send_key_str);
				if (!send_key_keycode)
					PANIC(send_key_node);
				mod_key.send_keycode = send_key_keycode;
			}
		}
		if (keycode >= 256)
			// always don't send for mouse-button modifiers
			mod_key.send_keycode = 0;

		tll_push_back(modifier.keys, mod_key);
	}
	tll_push_back(ctx->config->modifiers, modifier);
}

static key_signals_t
parse_key_signals(struct parser_context *ctx, yaml_node_t *key_action_node)
{
	key_signals_t result = {0};
	for (yaml_node_item_t *item =
		     key_action_node->data.sequence.items.start;
	     item < key_action_node->data.sequence.items.top; item++) {
		// (keybinds|gesturebinds)[*].(on_press|on_release|on_forward|on_backward)[*]
		yaml_node_t *elem_node =
			yaml_document_get_node(&ctx->doc, *item);
		if (elem_node->type != YAML_SCALAR_NODE)
			PANIC(elem_node);
		const char *key_signal_str = node_to_str(elem_node);

		enum {
			SIGNAL_PRESS = 1,
			SIGNAL_RELEASE = 2,
			SIGNAL_BOTH = 3,
		} signal_type;

		if (key_signal_str[0] == '+')
			signal_type = SIGNAL_PRESS;
		else if (key_signal_str[0] == '-')
			signal_type = SIGNAL_RELEASE;
		else
			signal_type = SIGNAL_BOTH;

		uint32_t keycode;
		if (signal_type == SIGNAL_PRESS
		    || signal_type == SIGNAL_RELEASE)
			keycode = keyname_to_keycode(ctx, &key_signal_str[1]);
		else
			keycode = keyname_to_keycode(ctx, key_signal_str);
		if (!keycode)
			PANIC(elem_node);

		struct key_signal signal = {.keycode = keycode, .press = true};
		if (signal_type & SIGNAL_PRESS)
			tll_push_back(result, signal);
		signal.press = false;
		if (signal_type & SIGNAL_RELEASE)
			tll_push_back(result, signal);
	}
	return result;
}

static key_signals_t
get_undo_key_signals(key_signals_t *signals)
{
	struct ryd_set pressed_keys = {0};
	tll_foreach(*signals, it) {
		if (it->item.press)
			ryd_set_add(&pressed_keys, it->item.keycode);
		else
			ryd_set_remove(&pressed_keys, it->item.keycode);
	}

	key_signals_t result = {0};
	for (int i = pressed_keys.size - 1; i >= 0; i--) {
		struct key_signal signal = {
			.keycode = pressed_keys.values[i],
			.press = false,
		};
		tll_push_back(result, signal);
	}

	return result;
}

static void
parse_keybind(struct parser_context *ctx, yaml_node_t *keybind_node)
{
	struct keybind keybind = {0};

	// "keybinds[*].key"
	yaml_node_t *key_node = get_node_by_key(ctx, keybind_node, "key");
	if (!key_node)
		PANIC(key_node);
	const char *key = node_to_str(key_node);
	uint32_t keycode = keyname_to_keycode(ctx, key);
	if (!keycode || keycode >= MAX_KEYCODE)
		PANIC(key_node);

	keybind.keycode = keycode;

	// "keybinds[*].modifiers"
	yaml_node_t *modifiers_node =
		get_node_by_key(ctx, keybind_node, "modifiers");
	if (modifiers_node) {
		if (modifiers_node->type != YAML_SEQUENCE_NODE)
			PANIC(modifiers_node);
		for (yaml_node_item_t *modifier_node_id =
			     modifiers_node->data.sequence.items.start;
		     modifier_node_id < modifiers_node->data.sequence.items.top;
		     modifier_node_id++) {
			// "entries.[*].modifiers[*]"
			yaml_node_t *modifier_node = yaml_document_get_node(
				&ctx->doc, *modifier_node_id);
			const char *modifier_name = node_to_str(modifier_node);

			struct modifier *modifier = NULL;
			tll_foreach(ctx->config->modifiers, it) {
				if (!strcmp(it->item.name, modifier_name)) {
					modifier = &it->item;
					break;
				}
			}
			if (!modifier)
				PANIC(modifier_node);

			tll_push_back(keybind.modifiers, modifier);
		};
	}

	// "gesturebinds[*].on_press"
	yaml_node_t *on_press_node =
		get_node_by_key(ctx, keybind_node, "on_press");
	if (!on_press_node)
		PANIC(keybind_node);
	if (on_press_node->type == YAML_SEQUENCE_NODE) {
		keybind.on_press.type = ACTION_KEY,
		keybind.on_press.signals =
			parse_key_signals(ctx, on_press_node);
	} else if (on_press_node->type == YAML_SCALAR_NODE) {
		keybind.on_press.type = ACTION_COMMAND;
		keybind.on_press.cmd = strdup(node_to_str(on_press_node));
	} else
		PANIC(on_press_node);

	// "gesturebinds[*].on_release"
	yaml_node_t *on_release_node =
		get_node_by_key(ctx, keybind_node, "on_release");
	if (!on_release_node) {
		// if on_release is undefined and on_press is key sequence,
		// set release action to key action that undoes pressed keys.
		if (keybind.on_press.type == ACTION_KEY) {
			keybind.on_release.type = ACTION_KEY;
			keybind.on_release.signals =
				get_undo_key_signals(&keybind.on_press.signals);
		}
	} else if (on_release_node->type == YAML_SEQUENCE_NODE) {
		keybind.on_release.type = ACTION_KEY;
		keybind.on_release.signals =
			parse_key_signals(ctx, on_release_node);
	} else if (on_release_node->type == YAML_SCALAR_NODE) {
		keybind.on_release.type = ACTION_COMMAND;
		keybind.on_release.cmd = strdup(node_to_str(on_release_node));
	} else {
		PANIC(on_release_node);
	}

	tll_push_back(ctx->config->keybinds, keybind);
}

static void
parse_gesturebind(struct parser_context *ctx, yaml_node_t *bind_node)
{
	struct gesturebind bind = {.repeat = false};

	// "gesturebinds[*].gesture"
	yaml_node_t *gesture_node = get_node_by_key(ctx, bind_node, "gesture");
	if (!gesture_node)
		PANIC(bind_node);
	const char *gesture_str = node_to_str(gesture_node);
	if (strcmp(gesture_str, "swipe")) {
		fprintf(stderr,
			"Only \"swipe\" gesture is currently implemented\n");
		PANIC(gesture_node);
	}

	// "gesturebinds[*].fingers"
	yaml_node_t *fingers_node = get_node_by_key(ctx, bind_node, "fingers");
	if (!fingers_node)
		PANIC(bind_node);
	bind.nr_fingers = node_to_int(fingers_node);
	if (bind.nr_fingers != 3 && bind.nr_fingers != 4) {
		fprintf(stderr, "3 or 4 is only allowed in \"fingers\"\n");
		PANIC(fingers_node);
	}

	// "gesturebinds[*].direction"
	yaml_node_t *direction_node =
		get_node_by_key(ctx, bind_node, "direction");
	if (!direction_node)
		PANIC(bind_node);
	const char *direction_str = node_to_str(direction_node);
	if (!strcmp(direction_str, "up"))
		bind.direction = DIRECTION_UP;
	else if (!strcmp(direction_str, "down"))
		bind.direction = DIRECTION_DOWN;
	else if (!strcmp(direction_str, "left"))
		bind.direction = DIRECTION_LEFT;
	else if (!strcmp(direction_str, "right"))
		bind.direction = DIRECTION_RIGHT;
	else
		PANIC(direction_node);

	// "gesturebinds[*].repeat"
	yaml_node_t *repeat_node = get_node_by_key(ctx, bind_node, "repeat");
	if (repeat_node)
		bind.repeat = node_to_bool(repeat_node);

	// "gesturebinds[*].on_forward"
	yaml_node_t *on_forward_node =
		get_node_by_key(ctx, bind_node, "on_forward");
	if (!on_forward_node)
		PANIC(bind_node);
	if (on_forward_node->type == YAML_SEQUENCE_NODE) {
		bind.on_forward.type = ACTION_KEY;
		bind.on_forward.signals =
			parse_key_signals(ctx, on_forward_node);
	} else if (on_forward_node->type == YAML_SCALAR_NODE) {
		bind.on_forward.type = ACTION_COMMAND;
		bind.on_forward.cmd = strdup(node_to_str(on_forward_node));
	} else {
		PANIC(on_forward_node);
	}

	// "gesturebinds[*].on_backward"
	yaml_node_t *on_backward_node =
		get_node_by_key(ctx, bind_node, "on_backward");
	if (!on_backward_node) {
		if (bind.on_forward.type == ACTION_KEY) {
			key_signals_t undo_signals =
				get_undo_key_signals(&bind.on_forward.signals);
			tll_foreach(undo_signals, it)
				tll_push_back(bind.on_forward.signals,
					      it->item);
			tll_free(undo_signals);
		}
	} else if (on_backward_node->type == YAML_SEQUENCE_NODE) {
		bind.on_backward.type = ACTION_KEY;
		bind.on_backward.signals =
			parse_key_signals(ctx, on_backward_node);
	} else if (on_backward_node->type == YAML_SCALAR_NODE) {
		bind.on_backward.type = ACTION_COMMAND;
		bind.on_backward.cmd = strdup(node_to_str(on_backward_node));
	} else {
		PANIC(on_backward_node);
	}

	tll_push_back(ctx->config->gesturebinds, bind);
}

static void
print_action(struct parser_context *ctx, struct action *action)
{
	switch (action->type) {
	case ACTION_NONE:
		printf("none\n");
		break;
	case ACTION_COMMAND:
		printf("%s\n", action->cmd);
		break;
	case ACTION_KEY:
		printf("[ ");
		tll_foreach(action->signals, it)
			printf("%s%s ", it->item.press ? "+" : "-",
			       keycode_to_keyname(ctx, it->item.keycode));
		printf("]\n");
		break;
	}
}

static void
print_config(struct parser_context *ctx)
{
	struct config *config = ctx->config;

	printf("modifiers:\n");
	tll_foreach(config->modifiers, mod_it) {
		printf("  %s:\n", mod_it->item.name);
		tll_foreach(mod_it->item.keys, key_it) {
			uint32_t keycode = key_it->item.keycode;
			uint32_t send_keycode = key_it->item.send_keycode;
			printf("    - key: %s\n",
			       keycode_to_keyname(ctx, keycode));
			printf("      send_key: %s\n",
			       send_keycode
				       ? keycode_to_keyname(ctx, send_keycode)
				       : "false");
		}
	}

	printf("keybinds:\n");
	tll_foreach(config->keybinds, bind_it) {
		printf("  - key: %s\n",
		       keycode_to_keyname(ctx, bind_it->item.keycode));
		printf("    modifiers: [ ");
		tll_foreach(bind_it->item.modifiers, mod_it)
			printf("%s ", mod_it->item->name);
		printf("]\n");
		printf("    on_press: ");
		print_action(ctx, &bind_it->item.on_press);
		printf("    on_release: ");
		print_action(ctx, &bind_it->item.on_release);
	}

	printf("gesturebinds:\n");
	tll_foreach(config->gesturebinds, bind_it) {
		printf("  - gesture: %s\n", "swipe"); // currently fixed
		printf("    fingers: %d\n", bind_it->item.nr_fingers);
		printf("    direction: %s\n",
		       bind_it->item.direction == DIRECTION_UP	    ? "up"
		       : bind_it->item.direction == DIRECTION_DOWN  ? "down"
		       : bind_it->item.direction == DIRECTION_LEFT  ? "left"
		       : bind_it->item.direction == DIRECTION_RIGHT ? "right"
								    : "?");
		printf("    repeat: %s\n",
		       bind_it->item.repeat ? "true" : "false");
		printf("    on_forward: ");
		print_action(ctx, &bind_it->item.on_forward);
		printf("    on_backward: ");
		print_action(ctx, &bind_it->item.on_backward);
	}
}

void
config_init(struct server *server)
{
	struct config *config = &server->config;

	config->swipe_thr = 50.;
	config->key_interval = 0.;
	config->key_repeat_delay = 0.5;
	config->key_repeat_interval = 0.03333;

	FILE *fp;
	fp = fopen("config.yml", "r");
	if (!fp)
		fp = fopen("/etc/rydeen/config.yml", "r");
	if (!fp) {
		fprintf(stderr, "config file not present\n");
		exit(1);
	}

	struct parser_context ctx = {.config = config};
	yaml_parser_initialize(&ctx.parser);
	yaml_parser_set_input_file(&ctx.parser, fp);
	yaml_parser_load(&ctx.parser, &ctx.doc);

	yaml_node_t *root_node = yaml_document_get_root_node(&ctx.doc);

	// "general"
	yaml_node_t *general_node = get_node_by_key(&ctx, root_node, "general");
	if (general_node)
		parse_general(&ctx, general_node);

	ctx.xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	ctx.keymap = xkb_keymap_new_from_names(ctx.xkb_ctx, &ctx.keyboard,
					       XKB_KEYMAP_COMPILE_NO_FLAGS);

	// "modifiers"
	yaml_node_t *modifiers_node =
		get_node_by_key(&ctx, root_node, "modifiers");
	if (modifiers_node) {
		if (modifiers_node->type != YAML_MAPPING_NODE)
			PANIC(modifiers_node);
		for (yaml_node_pair_t *modifier_kv =
			     modifiers_node->data.mapping.pairs.start;
		     modifier_kv < modifiers_node->data.mapping.pairs.top;
		     modifier_kv++)
			parse_modifier(&ctx, modifier_kv);
	}

	// "keybinds"
	yaml_node_t *keybinds_node =
		get_node_by_key(&ctx, root_node, "keybinds");
	if (keybinds_node) {
		if (keybinds_node->type != YAML_SEQUENCE_NODE)
			PANIC(keybinds_node);
		for (yaml_node_item_t *keybind_node_id =
			     keybinds_node->data.sequence.items.start;
		     keybind_node_id < keybinds_node->data.sequence.items.top;
		     keybind_node_id++) {
			parse_keybind(&ctx,
				      yaml_document_get_node(&ctx.doc,
							     *keybind_node_id));
		}
	}

	// "gesturebinds"
	yaml_node_t *gesturebinds_node =
		get_node_by_key(&ctx, root_node, "gesturebinds");
	if (gesturebinds_node) {
		if (gesturebinds_node->type != YAML_SEQUENCE_NODE)
			PANIC(gesturebinds_node);
		for (yaml_node_item_t *bind_node_id =
			     gesturebinds_node->data.sequence.items.start;
		     bind_node_id < gesturebinds_node->data.sequence.items.top;
		     bind_node_id++) {
			parse_gesturebind(
				&ctx, yaml_document_get_node(&ctx.doc,
							     *bind_node_id));
		}
	}

	if (DEBUG)
		print_config(&ctx);

	yaml_parser_delete(&ctx.parser);
	yaml_document_delete(&ctx.doc);
	xkb_keymap_unref(ctx.keymap);
	xkb_context_unref(ctx.xkb_ctx);
	fclose(fp);
}

static void
free_action(struct action *action)
{
	switch (action->type) {
	case ACTION_KEY:
		tll_free(action->signals);
		break;
	case ACTION_COMMAND:
		free((char *)action->cmd);
		break;
	default:
		break;
	}
}

void
config_finish(struct server *server)
{
	struct config *config = &server->config;

	tll_foreach(config->modifiers, it) {
		tll_free(it->item.keys);
		free((char *)it->item.name);
	}
	tll_free(config->modifiers);
	tll_foreach(config->keybinds, it) {
		tll_free(it->item.modifiers);
		free_action(&it->item.on_press);
		free_action(&it->item.on_release);
	}
	tll_free(config->keybinds);
	tll_foreach(config->gesturebinds, it) {
		free_action(&it->item.on_forward);
		free_action(&it->item.on_backward);
	}
	tll_free(config->gesturebinds);

	*config = (struct config){0};
}
