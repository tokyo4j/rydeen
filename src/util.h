#include "config.h"
#include <stdbool.h>
#include <stdint.h>

#define debug(...) \
	do { \
		if (DEBUG) \
			fprintf(stderr, __VA_ARGS__); \
	} while (0)

#define ARRAY_SIZE(arr) (int)(sizeof(arr) / sizeof((arr)[0]))
#define znew(sample) calloc(1, sizeof(sample))

#define RYD_SET_MAX_SIZE 16

struct ryd_set {
	uint32_t values[RYD_SET_MAX_SIZE];
	int size;
};

bool ryd_set_contains(struct ryd_set *set, uint32_t value);
void ryd_set_add(struct ryd_set *set, uint32_t value);
void ryd_set_remove(struct ryd_set *set, uint32_t value);

enum direction {
	DIRECTION_NONE,
	DIRECTION_UP,
	DIRECTION_RIGHT,
	DIRECTION_DOWN,
	DIRECTION_LEFT,
};

enum direction direction_opposite(enum direction dir);
