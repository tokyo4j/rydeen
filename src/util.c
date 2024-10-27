#include "util.h"
#include <assert.h>

bool
ryd_set_contains(struct ryd_set *set, uint32_t value)
{
	for (int i = 0; i < set->size; ++i) {
		if (set->values[i] == value)
			return true;
	}
	return false;
}

void
ryd_set_add(struct ryd_set *set, uint32_t value)
{
	if (ryd_set_contains(set, value))
		return;
	if (set->size >= RYD_SET_MAX_SIZE)
		return;
	set->values[set->size++] = value;
}

void
ryd_set_remove(struct ryd_set *set, uint32_t value)
{
	bool shifting = false;

	for (int i = 0; i < RYD_SET_MAX_SIZE; ++i) {
		if (set->values[i] == value) {
			--set->size;
			shifting = true;
		}
		if (shifting) {
			set->values[i] = i < RYD_SET_MAX_SIZE - 1
						 ? set->values[i + 1]
						 : 0;
		}
	}
}

enum direction
direction_opposite(enum direction dir)
{
	switch (dir) {
	case DIRECTION_NONE:
		return DIRECTION_NONE;
	case DIRECTION_UP:
		return DIRECTION_DOWN;
	case DIRECTION_DOWN:
		return DIRECTION_UP;
	case DIRECTION_LEFT:
		return DIRECTION_RIGHT;
	case DIRECTION_RIGHT:
		return DIRECTION_LEFT;
	default:
		assert(false && "unreachable");
	}
}
