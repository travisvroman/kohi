#include "nav_grid.h"

#include "containers/darray.h"
#include "core/agent_channel.h"
#include "debug/kassert.h"
#include "logger.h"
#include "math/kmath.h"
#include "memory/kmemory.h"
#include "strings/kstring.h"

#define COST_STRAIGHT 10
#define COST_DIAGONAL 14

struct nav_grid {
	u32 width;
	u32 height;
	f32 cell_size;
	f32 origin_x;
	f32 origin_y;
	u8 *blocked;
};

// The eight neighbours in the order they are tried: N, E, S, W, then NE, SE, SW, NW.
static const i32 step_x[8] = {0, 1, 0, -1, 1, 1, -1, -1};
static const i32 step_y[8] = {-1, 0, 1, 0, -1, 1, 1, -1};

/** @brief An entry in the open list. Entries are ordered by f, then h, then cell index, so no two ever tie. */
typedef struct open_entry {
	i32 f;
	i32 h;
	u32 index;
} open_entry;

static b8 open_before (open_entry a, open_entry b) {
	if (a.f != b.f) {
		return a.f < b.f;
	}
	if (a.h != b.h) {
		return a.h < b.h;
	}
	return a.index < b.index;
}

static void open_push (open_entry **heap, open_entry entry) {
	darray_push(*heap, &entry);
	u32 i = darray_length(*heap) - 1;
	open_entry *items = *heap;
	while (i > 0) {
		u32 parent = (i - 1) / 2;
		if (!open_before(items[i], items[parent])) {
			break;
		}
		open_entry swap = items[i];
		items[i] = items[parent];
		items[parent] = swap;
		i = parent;
	}
}

static open_entry open_pop (open_entry **heap) {
	open_entry *items = *heap;
	open_entry top = items[0];
	u32 count = darray_length(items) - 1;
	items[0] = items[count];
	darray_length_set(items, count);
	u32 i = 0;
	for (;;) {
		u32 left = i * 2 + 1;
		u32 right = left + 1;
		u32 best = i;
		if (left < count && open_before(items[left], items[best])) {
			best = left;
		}
		if (right < count && open_before(items[right], items[best])) {
			best = right;
		}
		if (best == i) {
			break;
		}
		open_entry swap = items[i];
		items[i] = items[best];
		items[best] = swap;
		i = best;
	}
	return top;
}

/** @brief The cost of the cheapest path between two cells on an empty grid (octile distance), or straight-line distance with no diagonals. */
static i32 heuristic (i32 x0, i32 y0, i32 x1, i32 y1, b8 diagonal) {
	i32 dx = x1 > x0 ? x1 - x0 : x0 - x1;
	i32 dy = y1 > y0 ? y1 - y0 : y0 - y1;
	if (!diagonal) {
		return COST_STRAIGHT * (dx + dy);
	}
	return COST_STRAIGHT * (dx + dy) + (COST_DIAGONAL - 2 * COST_STRAIGHT) * KMIN(dx, dy);
}

nav_grid *nav_grid_create (u32 width, u32 height, f32 cell_size, f32 origin_x, f32 origin_y) {
	KASSERT(width > 0 && height > 0 && cell_size > 0.0f);
	nav_grid *grid = kallocate(sizeof(nav_grid), MEMORY_TAG_GAME);
	grid->width = width;
	grid->height = height;
	grid->cell_size = cell_size;
	grid->origin_x = origin_x;
	grid->origin_y = origin_y;
	grid->blocked = kallocate((u64)width * height, MEMORY_TAG_GAME);
	return grid;
}

void nav_grid_destroy (nav_grid *grid) {
	if (grid) {
		kfree(grid->blocked);
		kfree(grid);
	}
}

u32 nav_grid_width (const nav_grid *grid) {
	return grid->width;
}

u32 nav_grid_height (const nav_grid *grid) {
	return grid->height;
}

static b8 in_grid (const nav_grid *grid, i32 x, i32 y) {
	return x >= 0 && y >= 0 && (u32)x < grid->width && (u32)y < grid->height;
}

b8 nav_grid_blocked_get (const nav_grid *grid, i32 x, i32 y) {
	return !in_grid(grid, x, y) || grid->blocked[(u32)y * grid->width + (u32)x];
}

void nav_grid_blocked_set (nav_grid *grid, i32 x, i32 y, b8 blocked) {
	if (in_grid(grid, x, y)) {
		grid->blocked[(u32)y * grid->width + (u32)x] = blocked ? 1 : 0;
	}
}

void nav_grid_blocked_rect_set (nav_grid *grid, i32 x, i32 y, u32 w, u32 h, b8 blocked) {
	for (u32 j = 0; j < h; ++j) {
		for (u32 i = 0; i < w; ++i) {
			nav_grid_blocked_set(grid, x + (i32)i, y + (i32)j, blocked);
		}
	}
}

nav_cell nav_grid_cell_from_world (const nav_grid *grid, f32 world_x, f32 world_y) {
	return (nav_cell){(i32)kfloor((world_x - grid->origin_x) / grid->cell_size), (i32)kfloor((world_y - grid->origin_y) / grid->cell_size)};
}

void nav_grid_cell_centre (const nav_grid *grid, nav_cell cell, f32 *out_world_x, f32 *out_world_y) {
	*out_world_x = grid->origin_x + ((f32)cell.x + 0.5f) * grid->cell_size;
	*out_world_y = grid->origin_y + ((f32)cell.y + 0.5f) * grid->cell_size;
}

nav_cell *nav_grid_find_path (const nav_grid *grid, nav_cell from, nav_cell to, b8 diagonal, u32 *out_count) {
	*out_count = 0;
	if (nav_grid_blocked_get(grid, from.x, from.y) || nav_grid_blocked_get(grid, to.x, to.y)) {
		return 0;
	}

	u32 cell_count = grid->width * grid->height;
	i32 *cost = kallocate(sizeof(i32) * cell_count, MEMORY_TAG_GAME); // cheapest known cost from the start, 0 = not reached
	i32 *parent = kallocate(sizeof(i32) * cell_count, MEMORY_TAG_GAME);
	u8 *closed = kallocate(cell_count, MEMORY_TAG_GAME);
	open_entry *open = darray_create(open_entry);

	u32 start = (u32)from.y * grid->width + (u32)from.x;
	u32 goal = (u32)to.y * grid->width + (u32)to.x;
	cost[start] = 1; // costs are stored plus one so that 0 can mean "not reached"
	parent[start] = -1;
	i32 start_h = heuristic(from.x, from.y, to.x, to.y, diagonal);
	open_push(&open, (open_entry){.f = start_h, .h = start_h, .index = start});

	b8 found = false;
	u32 directions = diagonal ? 8 : 4;
	while (darray_length(open) > 0) {
		open_entry current = open_pop(&open);
		if (closed[current.index]) {
			continue; // a cheaper entry for this cell was already taken
		}
		closed[current.index] = 1;
		if (current.index == goal) {
			found = true;
			break;
		}
		i32 cx = (i32)(current.index % grid->width);
		i32 cy = (i32)(current.index / grid->width);
		for (u32 d = 0; d < directions; ++d) {
			i32 nx = cx + step_x[d];
			i32 ny = cy + step_y[d];
			if (nav_grid_blocked_get(grid, nx, ny)) {
				continue;
			}
			b8 is_diagonal = d >= 4;
			if (is_diagonal && (nav_grid_blocked_get(grid, cx + step_x[d], cy) || nav_grid_blocked_get(grid, cx, cy + step_y[d]))) {
				continue; // no cutting a blocked corner
			}
			u32 next = (u32)ny * grid->width + (u32)nx;
			if (closed[next]) {
				continue;
			}
			i32 next_cost = cost[current.index] + (is_diagonal ? COST_DIAGONAL : COST_STRAIGHT);
			if (cost[next] != 0 && next_cost >= cost[next]) {
				continue;
			}
			cost[next] = next_cost;
			parent[next] = (i32)current.index;
			i32 h = heuristic(nx, ny, to.x, to.y, diagonal);
			open_push(&open, (open_entry){.f = next_cost - 1 + h, .h = h, .index = next});
		}
	}

	nav_cell *path = 0;
	if (found) {
		u32 length = 0;
		for (i32 at = (i32)goal; at != -1; at = parent[at]) {
			length++;
		}
		path = kallocate(sizeof(nav_cell) * length, MEMORY_TAG_GAME);
		u32 write = length;
		for (i32 at = (i32)goal; at != -1; at = parent[at]) {
			write--;
			path[write] = (nav_cell){at % (i32)grid->width, at / (i32)grid->width};
		}
		*out_count = length;
	}

	darray_destroy(open);
	kfree(closed);
	kfree(parent);
	kfree(cost);
	return path;
}

// Agent commands

static nav_grid *agent_grid;

static b8 agent_cell_get (const agent_request *request, agent_reply *reply, const char *x_key, const char *y_key, nav_cell *out) {
	f64 x = 0;
	f64 y = 0;
	if (!agent_request_number_get(request, x_key, &x) || !agent_request_number_get(request, y_key, &y)) {
		char *message = string_format("needs \"%s\" and \"%s\" (cell numbers)", x_key, y_key);
		agent_reply_error(reply, message);
		string_free(message);
		return false;
	}
	*out = (nav_cell){(i32)x, (i32)y};
	return true;
}

static b8 agent_cmd_nav_block (const agent_request *request, agent_reply *reply, void *listener) {
	nav_cell corner;
	if (!agent_cell_get(request, reply, "x", "y", &corner)) {
		return false;
	}
	f64 w = 1;
	f64 h = 1;
	agent_request_number_get(request, "w", &w);
	agent_request_number_get(request, "h", &h);
	b8 blocked = true;
	agent_request_bool_get(request, "blocked", &blocked);
	if (w < 1 || h < 1) {
		agent_reply_error(reply, "\"w\" and \"h\" must be at least 1");
		return false;
	}
	nav_grid_blocked_rect_set(agent_grid, corner.x, corner.y, (u32)w, (u32)h, blocked);
	return true;
}

static b8 agent_cmd_nav_path (const agent_request *request, agent_reply *reply, void *listener) {
	nav_cell from;
	nav_cell to;
	if (!agent_cell_get(request, reply, "from_x", "from_y", &from) || !agent_cell_get(request, reply, "to_x", "to_y", &to)) {
		return false;
	}
	b8 diagonal = true;
	agent_request_bool_get(request, "diagonal", &diagonal);

	u32 count = 0;
	nav_cell *path = nav_grid_find_path(agent_grid, from, to, diagonal, &count);
	if (!path) {
		agent_reply_error(reply, "no path: an end is blocked or outside the grid, or the goal can't be reached");
		return false;
	}
	char *json = 0;
	agent_json_append(&json, "[");
	for (u32 i = 0; i < count; ++i) {
		if (i) {
			agent_json_append(&json, ",");
		}
		agent_json_append(&json, "[");
		agent_json_append_f64(&json, path[i].x);
		agent_json_append(&json, ",");
		agent_json_append_f64(&json, path[i].y);
		agent_json_append(&json, "]");
	}
	agent_json_append(&json, "]");
	agent_reply_int_add(reply, "length", count);
	agent_reply_json_add(reply, "cells", json);
	string_free(json);
	kfree(path);
	return true;
}

void nav_grid_agent_commands_register (nav_grid *grid) {
	agent_grid = grid;
	agent_command_register("nav_block", "Blocks or clears cells of the navigation grid: {\"x\", \"y\"} corner, {\"w\", \"h\"} size (default 1), {\"blocked\"} default true.", 0, agent_cmd_nav_block);
	agent_command_register("nav_path", "Finds the cheapest path between cells: {\"from_x\", \"from_y\", \"to_x\", \"to_y\"}, {\"diagonal\"} default true. Replies with length and cells as [[x, y], ...], or an error if there is none.", 0, agent_cmd_nav_path);
}
