#include "action.h"
#include "array.h"
#include "gesture_consumer.h"
#include "key_consumer.h"
#include "rydeen.h"
#include <limits.h>
#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <string.h>
#include <xkbcommon/xkbcommon.h>
#include <yaml.h>

struct modifier_table_entry {
    const char *name;
    struct modifier *modifier;
};
ARRAY_DEF(struct modifier_table_entry, modifier_table_entry);

double config_swipe_thr = 50.;
double config_pinch_thr = 0.2;
double config_key_interval = 0.;
double config_key_repeat_delay = 0.5;
double config_key_repeat_interval = 0.03333;
const char *config_shell = "sh";
static struct xkb_rule_names config_keyboard;
static struct xkb_context *xkb_ctx;
static struct xkb_keymap *keymap;
static yaml_document_t doc;
static struct array_modifier_table_entry modifier_table;

#ifdef DEBUG
static void print_config();
#endif

#define PANIC(node) \
    do { \
        fprintf(stderr, "Falied to parse config at %ld:%ld", \
                (node)->start_mark.line + 1, (node)->start_mark.column + 1); \
        exit(1); \
    } while (0)

static yaml_node_t *get_node_by_key(const yaml_node_t *mapping_node,
                                    const char *key) {
    if (mapping_node->type != YAML_MAPPING_NODE)
        return NULL;
    for (yaml_node_pair_t *pair = mapping_node->data.mapping.pairs.start;
         pair < mapping_node->data.mapping.pairs.top; pair++) {
        yaml_node_t *key_node = yaml_document_get_node(&doc, pair->key);
        if (!strcmp((char *)key_node->data.scalar.value, key))
            return yaml_document_get_node(&doc, pair->value);
    }
    return NULL;
}

static inline double node_to_double(yaml_node_t *node) {
    if (node->type != YAML_SCALAR_NODE)
        PANIC(node);

    char *cast_err;
    double result = strtod((char *)node->data.scalar.value, &cast_err);
    if (*cast_err)
        PANIC(node);
    return result;
}

static inline int node_to_int(yaml_node_t *node) {
    if (node->type != YAML_SCALAR_NODE)
        PANIC(node);

    char *cast_err;
    long result = strtol((char *)node->data.scalar.value, &cast_err, 10);
    if (*cast_err || result > INT_MAX || result < INT_MIN)
        PANIC(node);
    return (int)result;
}

static inline const char *node_to_str(yaml_node_t *node) {
    if (node->type != YAML_SCALAR_NODE)
        PANIC(node);
    return (const char *)((node)->data.scalar.value);
}

static inline bool node_to_bool(yaml_node_t *node) {
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

// inefficient implementation, but it's ok
static uint32_t keyname_to_keycode(const char *keyname) {
    for (int i = 0; i < ARRAY_SIZE(mouse_keysyms); i++)
        if (!strcmp(keyname, mouse_keysyms[i].keyname))
            return mouse_keysyms[i].keycode;

    for (uint32_t i = 0; i < MAX_KEYCODE; i++) {
        const xkb_keysym_t *syms;
        int syms_len =
            xkb_keymap_key_get_syms_by_level(keymap, i + 8, 0, 0, &syms);
        if (!syms_len)
            continue;
        for (int j = 0; j < syms_len; j++) {
            char sym_name[64];
            if (!xkb_keysym_get_name(syms[j], sym_name, sizeof(sym_name)))
                continue;
            if (!strcmp(sym_name, keyname))
                return i;
        }
    }
    return 0;
}

static const char *keycode_to_keyname(uint32_t keycode) {
    for (int i = 0; i < ARRAY_SIZE(mouse_keysyms); i++)
        if (keycode == mouse_keysyms[i].keycode)
            return mouse_keysyms[i].keyname;

    const xkb_keysym_t *syms;
    int syms_len =
        xkb_keymap_key_get_syms_by_level(keymap, keycode + 8, 0, 0, &syms);
    if (!syms_len)
        return NULL;
    char *sym_name = malloc(64);
    if (!xkb_keysym_get_name(syms[0], sym_name, 64))
        return NULL;
    return sym_name;
}

static void parse_config_general(const yaml_node_t *general_node) {
    // "general.key_interval"
    yaml_node_t *key_interval_node =
        get_node_by_key(general_node, "key_interval");
    if (key_interval_node) {
        config_key_interval = node_to_double(key_interval_node);
    }

    // "general.key_repeat_delay"
    yaml_node_t *key_repeat_delay_node =
        get_node_by_key(general_node, "key_repeat_delay");
    if (key_repeat_delay_node) {
        config_key_repeat_delay = node_to_double(key_repeat_delay_node);
    }

    // "general.key_repeat_interval"
    yaml_node_t *key_repeat_interval_node =
        get_node_by_key(general_node, "key_repeat_interval");
    if (key_repeat_interval_node) {
        config_key_repeat_interval = node_to_double(key_repeat_interval_node);
    }

    // "general.swipe_threshold"
    yaml_node_t *swipe_thr_node =
        get_node_by_key(general_node, "swipe_threshold");
    if (swipe_thr_node) {
        config_swipe_thr = node_to_double(swipe_thr_node);
    }

    // "general.pinch_threshold"
    yaml_node_t *pinch_thr_node =
        get_node_by_key(general_node, "pinch_threshold");
    if (pinch_thr_node)
        config_pinch_thr = node_to_double(pinch_thr_node);

    // "general.keyboard"
    yaml_node_t *keyboard_node = get_node_by_key(general_node, "keyboard");
    if (keyboard_node) {
        // "general.keyboard.rules"
        yaml_node_t *rules_node = get_node_by_key(keyboard_node, "rules");
        if (rules_node)
            config_keyboard.rules = node_to_str(rules_node);
        // "general.keyboard.model"
        yaml_node_t *model_node = get_node_by_key(keyboard_node, "model");
        if (model_node)
            config_keyboard.model = node_to_str(model_node);
        // "general.keyboard.layout"
        yaml_node_t *layout_node = get_node_by_key(keyboard_node, "layout");
        if (layout_node)
            config_keyboard.layout = node_to_str(layout_node);
        // "general.keyboard.variant"
        yaml_node_t *variant_node = get_node_by_key(keyboard_node, "variant");
        if (variant_node)
            config_keyboard.variant = node_to_str(variant_node);
        // "general.keyboard.options"
        yaml_node_t *options_node = get_node_by_key(keyboard_node, "options");
        if (options_node)
            config_keyboard.options = node_to_str(options_node);
    }

    // "general.shell"
    yaml_node_t *shell_node = get_node_by_key(general_node, "shell");
    if (shell_node)
        config_shell = node_to_str(shell_node);
}

static void parse_config_modifier(const yaml_node_pair_t *modifier_kv) {
    struct modifier *modifier = calloc(sizeof(*modifier), 1);

    const char *modifier_name =
        node_to_str(yaml_document_get_node(&doc, modifier_kv->key));

#ifdef DEBUG
    modifier->_debug_name = modifier_name;
#endif

    // "modifiers.(modifier_name)"
    yaml_node_t *modifier_val_node =
        yaml_document_get_node(&doc, modifier_kv->value);
    if (modifier_val_node->type != YAML_SEQUENCE_NODE)
        PANIC(modifier_val_node);

    for (yaml_node_item_t *item = modifier_val_node->data.sequence.items.start;
         item < modifier_val_node->data.sequence.items.top; item++) {
        // "modifiers.(modifier_name)[*]"
        yaml_node_t *modifier_item_node = yaml_document_get_node(&doc, *item);
        if (modifier_item_node->type != YAML_MAPPING_NODE)
            PANIC(modifier_item_node);
        // "modifiers.(modifier_name)[*].key"
        yaml_node_t *key_node = get_node_by_key(modifier_item_node, "key");
        const char *key_name = node_to_str(key_node);
        uint32_t keycode = keyname_to_keycode(key_name);
        if (!keycode)
            PANIC(key_node);
        struct key_binding *binding = &key_bindings[keycode];
        if (binding->type)
            PANIC(key_node);
        binding->type = KEY_BINDING_MODIFIER;
        binding->modifier = modifier;

        // "modifiers.(modifier_name)[*].transfer"
        binding->transfer = IDENTITY_TRANSFER;
        yaml_node_t *transfer_node =
            get_node_by_key(modifier_item_node, "transfer");
        if (transfer_node) {
            const char *transfer_str = node_to_str(transfer_node);
            if (!strcmp(transfer_str, "true"))
                binding->transfer = IDENTITY_TRANSFER;
            else if (!strcmp(transfer_str, "false"))
                binding->transfer = 0;
            else {
                uint32_t keycode = keyname_to_keycode(transfer_str);
                if (!keycode)
                    PANIC(transfer_node);
                binding->transfer = keycode;
            }
        }
        if (keycode >= 256)
            // always false for mouse buttons
            binding->transfer = false;
    }
    array_add(&modifier_table,
              ((struct modifier_table_entry){.name = modifier_name,
                                             .modifier = modifier}));
}

static struct array_key_signal
parse_config_key_signals(yaml_node_t *key_action_node) {
    struct array_key_signal result = {0};
    for (yaml_node_item_t *elem_node_id =
             key_action_node->data.sequence.items.start;
         elem_node_id < key_action_node->data.sequence.items.top;
         elem_node_id++) {
        // entries[*].(on_press,on_release,on_start, on_reverse)[*]
        yaml_node_t *elem_node = yaml_document_get_node(&doc, *elem_node_id);
        if (elem_node->type != YAML_SCALAR_NODE)
            PANIC(elem_node);
        const char *key_signal_str = node_to_str(elem_node);

        const int PRESS = 0;
        const int RELEASE = 1;
        const int COMBINED = 2;

        int type;
        if (key_signal_str[0] == '+')
            type = PRESS;
        else if (key_signal_str[0] == '-')
            type = RELEASE;
        else
            type = COMBINED;

        uint32_t keycode;
        if (type == PRESS || type == RELEASE)
            keycode = keyname_to_keycode(&key_signal_str[1]);
        else
            keycode = keyname_to_keycode(key_signal_str);
        if (!keycode)
            PANIC(elem_node);

        if (type == PRESS || type == COMBINED)
            array_add(&result,
                      ((struct key_signal){.keycode = keycode, .state = 1}));
        if (type == RELEASE || type == COMBINED)
            array_add(&result,
                      ((struct key_signal){.keycode = keycode, .state = 0}));
    }
    return result;
}

static struct array_key_signal
get_undo_key_signals(struct array_key_signal *signals) {
    struct array_key_signal result = {0};
    array_foreach_ref(signals, signal) {
        // entries[*].(on_press,on_release,on_start,on_reverse)[*]
        if (signal->state == 0) {
            for (int i = result.size - 1; i >= 0; i--)
                if (result.elems[i].keycode == signal->keycode)
                    array_remove(&result, i);
        } else {
            array_add(&result, ((struct key_signal){.keycode = signal->keycode,
                                                    .state = 0}));
        }
    }
    array_reverse(&result);
    return result;
}

static void parse_config_key_entry(yaml_node_t *entry_node) {
    struct key_entry *entry = calloc(sizeof(*entry), 1);

    // "entries.[*].key"
    yaml_node_t *key_node = get_node_by_key(entry_node, "key");
    const char *key = node_to_str(key_node);
    uint32_t keycode = keyname_to_keycode(key);
    if (!keycode || keycode >= MAX_KEYCODE)
        PANIC(key_node);

#ifdef DEBUG
    entry->_debug_keycode = keycode;
#endif

    struct key_binding *binding = &key_bindings[keycode];
    if (binding->type == KEY_BINDING_NULL) {
        binding->type = KEY_BINDING_ENTRIES;
        if (keycode < 256) {
            // if entry is being bound to the keycode (not mouse button) for the
            // first time, bind default entry (ex. a->a, b->b..) beforehand, as
            // entries added later are priotized.
            struct key_entry *default_entry = calloc(sizeof(*entry), 1);
            default_entry->press_action.type = ACTION_KEY;
            array_add(&default_entry->press_action.key_signals,
                      ((struct key_signal){.keycode = keycode, .state = 1}));
            default_entry->release_action.type = ACTION_KEY;
            array_add(&default_entry->release_action.key_signals,
                      ((struct key_signal){.keycode = keycode, .state = 0}));
#ifdef DEBUG
            default_entry->_debug_keycode = keycode;
#endif
            array_add(&binding->entries, default_entry);
        }
    }
    if (binding->type != KEY_BINDING_ENTRIES)
        PANIC(entry_node);
    array_add(&binding->entries, entry);

    // "entries[*].modifiers"
    yaml_node_t *modifiers_node = get_node_by_key(entry_node, "modifiers");
    if (modifiers_node) {
        if (modifiers_node->type != YAML_SEQUENCE_NODE)
            PANIC(modifiers_node);
        for (yaml_node_item_t *modifier_node_id =
                 modifiers_node->data.sequence.items.start;
             modifier_node_id < modifiers_node->data.sequence.items.top;
             modifier_node_id++) {
            // "entries.[*].modifiers[*]"
            yaml_node_t *modifier_node =
                yaml_document_get_node(&doc, *modifier_node_id);
            const char *modifier_name = node_to_str(modifier_node);

            struct modifier *modifier = NULL;
            array_foreach_ref(&modifier_table, entry) {
                if (!strcmp(modifier_name, entry->name)) {
                    modifier = entry->modifier;
                    break;
                }
            }
            if (!modifier)
                PANIC(modifier_node);

            array_add(&entry->modifiers, modifier);
            array_add(&modifier->entries, entry);
        };
    }

    // "entries[*].on_press"
    yaml_node_t *on_press_node = get_node_by_key(entry_node, "on_press");
    if (!on_press_node)
        PANIC(entry_node);
    if (on_press_node->type == YAML_SEQUENCE_NODE)
        entry->press_action = (struct action){
            .type = ACTION_KEY,
            .key_signals = parse_config_key_signals(on_press_node)};
    else if (on_press_node->type == YAML_SCALAR_NODE)
        entry->press_action = (struct action){
            .type = ACTION_CMD, .cmd = node_to_str(on_press_node)};
    else
        PANIC(on_press_node);

    // "entries[*].on_release"
    yaml_node_t *on_release_node = get_node_by_key(entry_node, "on_release");
    // if on_release is undefined and on_press is key sequence,
    // set release action to key action that undoes pressed keys.
    if (!on_release_node) {
        if (entry->press_action.type == ACTION_KEY)
            entry->release_action =
                (struct action){.type = ACTION_KEY,
                                .key_signals = get_undo_key_signals(
                                    &entry->press_action.key_signals)};
    } else if (on_release_node->type == YAML_SEQUENCE_NODE)
        entry->release_action = (struct action){
            .type = ACTION_KEY,
            .key_signals = parse_config_key_signals(on_release_node)};
    else if (on_release_node->type == YAML_SCALAR_NODE)
        entry->release_action = (struct action){
            .type = ACTION_CMD, .cmd = node_to_str(on_release_node)};
    else
        PANIC(on_release_node);
}

static void parse_config_gesture_entry(yaml_node_t *entry_node) {
    // "entries[*].gesture"
    yaml_node_t *gesture_node = get_node_by_key(entry_node, "gesture");
    const char *gesture_str = node_to_str(gesture_node);
    int gesture_type;
    if (!strcmp(gesture_str, "swipe"))
        gesture_type = GESTURE_BINDING_SWIPE;
    else if (!strcmp(gesture_str, "pinch"))
        gesture_type = GESTURE_BINDING_PINCH;
    else
        gesture_type = GESTURE_BINDING_NULL;

    if (gesture_type == GESTURE_BINDING_PINCH) {
        fprintf(stderr, "Pinch action is not implemented yet.\n");
        exit(1);
    }

    // "entries[*].fingers"
    yaml_node_t *fingers_node = get_node_by_key(entry_node, "fingers");
    if (gesture_type == GESTURE_BINDING_PINCH && !fingers_node)
        PANIC(entry_node);
    int fingers = node_to_int(fingers_node);
    if (fingers != 3 && fingers != 4)
        PANIC(fingers_node);

    // "entries[*].direction"
    enum direction direction;
    yaml_node_t *direction_node = get_node_by_key(entry_node, "direction");
    if (!direction_node)
        PANIC(entry_node);
    const char *direction_str = node_to_str(direction_node);
    if (!strcmp(direction_str, "up"))
        direction = UP;
    else if (!strcmp(direction_str, "down"))
        direction = DOWN;
    else if (!strcmp(direction_str, "left"))
        direction = LEFT;
    else if (!strcmp(direction_str, "right"))
        direction = RIGHT;
    else
        PANIC(direction_node);

    struct gesture_binding *binding =
        GET_GESTURE_BINDING(direction, gesture_type, fingers);

    if (binding->forward_action.type != ACTION_NULL)
        PANIC(entry_node);

    // "entries[*].repeats"
    yaml_node_t *repeats_node = get_node_by_key(entry_node, "repeats");
    if (repeats_node) {
        bool repeats = node_to_bool(repeats_node);
        binding->repeats = repeats;
    }

    // "entries[*].on_start"
    yaml_node_t *on_start_node = get_node_by_key(entry_node, "on_start");
    if (!on_start_node)
        PANIC(entry_node);
    if (on_start_node->type == YAML_SEQUENCE_NODE)
        binding->forward_action = (struct action){
            .type = ACTION_KEY,
            .key_signals = parse_config_key_signals(on_start_node)};
    else if (on_start_node->type == YAML_SCALAR_NODE)
        binding->forward_action = (struct action){
            .type = ACTION_CMD, .cmd = node_to_str(on_start_node)};
    else
        PANIC(on_start_node);

    // "entries[*].on_reverse"
    yaml_node_t *on_reverse_node = get_node_by_key(entry_node, "on_reverse");
    if (!on_reverse_node) {
        if (binding->forward_action.type == ACTION_KEY) {
            struct array_key_signal undo_signals =
                get_undo_key_signals(&binding->forward_action.key_signals);
            array_concat(&binding->forward_action.key_signals, &undo_signals);
        }
    } else if (on_reverse_node->type == YAML_SEQUENCE_NODE)
        binding->backward_action = (struct action){
            .type = ACTION_KEY,
            .key_signals = parse_config_key_signals(on_reverse_node)};
    else if (on_reverse_node->type == YAML_SCALAR_NODE)
        binding->backward_action = (struct action){
            .type = ACTION_CMD, .cmd = node_to_str(on_reverse_node)};
    else
        PANIC(on_reverse_node);
}

static void parse_config_entry(yaml_node_item_t entry_node_id) {
    yaml_node_t *entry_node = yaml_document_get_node(&doc, entry_node_id);
    if (entry_node->type != YAML_MAPPING_NODE)
        PANIC(entry_node);
    bool is_key_entry = (bool)get_node_by_key(entry_node, "key");
    bool is_gesture_entry = (bool)get_node_by_key(entry_node, "gesture");
    if ((is_key_entry && is_gesture_entry) ||
        (!is_key_entry && !is_gesture_entry))
        PANIC(entry_node);

    if (is_key_entry)
        parse_config_key_entry(entry_node);
    else if (is_gesture_entry)
        parse_config_gesture_entry(entry_node);
}

void parse_config() {
    FILE *fp;
    fp = fopen("config.yml", "r");
    if (!fp)
        fp = fopen("/etc/rydeen/config.yml", "r");
    if (!fp) {
        fprintf(stderr, "config file not present\n");
        exit(1);
    }
    yaml_parser_t parser;

    yaml_parser_initialize(&parser);
    yaml_parser_set_input_file(&parser, fp);
    yaml_parser_load(&parser, &doc);

    yaml_node_t *root_node = yaml_document_get_root_node(&doc);

    // "general"
    yaml_node_t *general_node = get_node_by_key(root_node, "general");
    if (general_node)
        parse_config_general(general_node);

    xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    keymap = xkb_keymap_new_from_names(xkb_ctx, &config_keyboard,
                                       XKB_KEYMAP_COMPILE_NO_FLAGS);

    // "modifiers"
    yaml_node_t *modifiers_node = get_node_by_key(root_node, "modifiers");
    if (modifiers_node) {
        if (modifiers_node->type != YAML_MAPPING_NODE)
            PANIC(modifiers_node);
        for (yaml_node_pair_t *modifier_kv =
                 modifiers_node->data.mapping.pairs.start;
             modifier_kv < modifiers_node->data.mapping.pairs.top;
             modifier_kv++)
            parse_config_modifier(modifier_kv);
    }

    // "entries"
    yaml_node_t *entries_node = get_node_by_key(root_node, "entries");
    if (entries_node) {
        if (entries_node->type != YAML_SEQUENCE_NODE)
            PANIC(entries_node);
        for (yaml_node_item_t *entry_node_id =
                 entries_node->data.sequence.items.start;
             entry_node_id < entries_node->data.sequence.items.top;
             entry_node_id++) {
            parse_config_entry(*entry_node_id);
        }
    }

#ifdef DEBUG
    print_config();
#endif
}

#ifdef DEBUG
static void print_config() {
    puts("[modifiers]");
    array_foreach_ref(&modifier_table, entry) {
        debug("%s:\n", entry->name);
        debug("  entries:\n");
        struct modifier *modifier = entry->modifier;
        array_foreach(&modifier->entries, entry) {
            debug("    - %s\n", keycode_to_keyname(entry->_debug_keycode));
        }
    }
    puts("[key_bindings]");
    for (int i = 0; i < MAX_KEYCODE; i++) {
        struct key_binding *binding = &key_bindings[i];
        if (binding->type == KEY_BINDING_NULL)
            continue;
        debug("%s:\n", keycode_to_keyname(i));
        if (binding->type == KEY_BINDING_MODIFIER) {
            debug("  type: MODIFIER\n");
            debug("  name: %s\n", binding->modifier->_debug_name);
            debug(
                "  transfer: %s\n",
                binding->transfer == IDENTITY_TRANSFER ? "IDENTITY_TRANSFER" :
                binding->transfer == 0                 ? "NONE" :
                                         keycode_to_keyname(binding->transfer));
        } else if (binding->type == KEY_BINDING_ENTRIES) {
            debug("  type: ENTRIES\n");
            debug("  entries:\n");
            array_foreach(&binding->entries, entry) {
                debug("      modifiers:\n");
                array_foreach(&entry->modifiers, modifier) {
                    debug("        - %s\n", modifier->_debug_name);
                }
                debug("      press_action:\n");
                debug("        type: %s\n",
                      entry->press_action.type == ACTION_NULL ? "NULL" :
                      entry->press_action.type == ACTION_CMD  ? "CMD" :
                                                                "KEY");
                if (entry->press_action.type == ACTION_KEY) {
                    debug("        key_signals:\n");
                    array_foreach_ref(&entry->press_action.key_signals,
                                      signal) {
                        debug("          - %s %s\n",
                              signal->state ? "press" : "release",
                              keycode_to_keyname(signal->keycode));
                    }
                } else if (entry->press_action.type == ACTION_CMD) {
                    debug("      command: %s\n", entry->press_action.cmd);
                }

                debug("      release_action:\n");
                debug("        type: %s\n",
                      entry->release_action.type == ACTION_NULL ? "NULL" :
                      entry->release_action.type == ACTION_CMD  ? "CMD" :
                                                                  "KEY");
                if (entry->release_action.type == ACTION_KEY) {
                    debug("        key_signals:\n");
                    array_foreach_ref(&entry->release_action.key_signals,
                                      signal) {
                        debug("          - %s %s\n",
                              signal->state ? "press" : "release",
                              keycode_to_keyname(signal->keycode));
                    }
                } else if (entry->release_action.type == ACTION_CMD) {
                    debug("      command: %s\n", entry->release_action.cmd);
                }
            }
        }
    }
    puts("[gesture_bindings]");
    for (int i = 0; i < 32; i++) {
        struct gesture_binding *binding = &gesture_bindings[i];
        if (binding->forward_action.type == ACTION_NULL)
            continue;
        debug("  - type: %s\n",
              GET_GESTURE_TYPE(i) == GESTURE_BINDING_SWIPE ? "SWIPE" :
              GET_GESTURE_TYPE(i) == GESTURE_BINDING_PINCH ? "PINCH" :
                                                             "NULL");
        debug("    repeats: %s\n", binding->repeats ? "true" : "false");
        debug("    fingers: %d\n", GET_GESTURE_FINGERS(i));
        debug("    direction: %s\n",
              GET_GESTURE_DIRECTION(i) == UP   ? "UP" :
              GET_GESTURE_DIRECTION(i) == DOWN ? "DOWN" :
              GET_GESTURE_DIRECTION(i) == LEFT ? "LEFT" :
                                                 "RIGHT");
        debug("    forward_action:\n");
        debug("      type: %s\n",
              binding->forward_action.type == ACTION_NULL ? "NULL" :
              binding->forward_action.type == ACTION_CMD  ? "CMD" :
                                                            "KEY");
        if (binding->forward_action.type == ACTION_KEY) {
            debug("      key_signals:\n");
            array_foreach_ref(&binding->forward_action.key_signals, signal) {
                debug("        - %s %s\n", signal->state ? "press" : "release",
                      keycode_to_keyname(signal->keycode));
            }
        } else if (binding->forward_action.type == ACTION_CMD) {
            debug("      command: %s\n", binding->forward_action.cmd);
        }

        debug("    backward_action:\n");
        debug("      type: %s\n",
              binding->backward_action.type == ACTION_NULL ? "NULL" :
              binding->backward_action.type == ACTION_CMD  ? "CMD" :
                                                             "KEY");
        if (binding->backward_action.type == ACTION_KEY) {
            debug("      key_signals:\n");
            array_foreach_ref(&binding->backward_action.key_signals, signal) {
                debug("          - %s %s\n",
                      signal->state ? "press" : "release",
                      keycode_to_keyname(signal->keycode));
            }
        } else if (binding->backward_action.type == ACTION_CMD) {
            debug("      command: %s\n", binding->backward_action.cmd);
        }
    }
    puts("[general]");
    debug("key_interval: %lf\n", config_key_interval);
}
#endif // DEBUG
