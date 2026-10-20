#pragma once

#include "defines.h"

/**
 * @file nav_grid.h
 * @brief Grid pathfinding: A* over a flat grid of walkable and blocked cells.
 *
 * Results are deterministic: integer costs, a fixed neighbour order and a total order on ties mean
 * the same grid and endpoints give the same path on every machine. Costs are 10 for a step along an
 * axis and 14 for a diagonal. A diagonal step is not taken past a blocked corner.
 *
 * The grid is flat (x and y are cells). A game with a ground plane in x and z can use
 * nav_grid_cell_from_world and nav_grid_cell_centre to convert.
 */

typedef struct nav_grid nav_grid;

typedef struct nav_cell {
	i32 x;
	i32 y;
} nav_cell;

/** @brief Makes a grid of width x height walkable cells. cell_size is the world size of a cell; origin_x and origin_y are the world position of the corner of cell (0, 0). */
KAPI nav_grid *nav_grid_create (u32 width, u32 height, f32 cell_size, f32 origin_x, f32 origin_y);
KAPI void nav_grid_destroy (nav_grid *grid);

KAPI u32 nav_grid_width (const nav_grid *grid);
KAPI u32 nav_grid_height (const nav_grid *grid);

/** @brief Cells outside the grid count as blocked. */
KAPI b8 nav_grid_blocked_get (const nav_grid *grid, i32 x, i32 y);
/** @brief Sets one cell; does nothing outside the grid. */
KAPI void nav_grid_blocked_set (nav_grid *grid, i32 x, i32 y, b8 blocked);
/** @brief Sets a w x h block of cells with its corner at (x, y), clipped to the grid. */
KAPI void nav_grid_blocked_rect_set (nav_grid *grid, i32 x, i32 y, u32 w, u32 h, b8 blocked);

/** @brief The cell that holds a world position (x, y). It may be outside the grid. */
KAPI nav_cell nav_grid_cell_from_world (const nav_grid *grid, f32 world_x, f32 world_y);
KAPI void nav_grid_cell_centre (const nav_grid *grid, nav_cell cell, f32 *out_world_x, f32 *out_world_y);

/**
 * @brief Finds the cheapest path from one cell to another, both included. Returns a kallocate'd
 * array of *out_count cells (free it with kfree), or 0 with *out_count 0 if either end is blocked
 * or outside the grid, or the goal can't be reached. With diagonal false, steps are along axes only.
 */
KAPI nav_cell *nav_grid_find_path (const nav_grid *grid, nav_cell from, nav_cell to, b8 diagonal, u32 *out_count);

/**
 * @brief Adds the agent commands nav_block and nav_path for this grid, so an agent can edit the
 * grid and ask for paths. The grid must outlive the engine run. Registering another grid replaces
 * the first.
 */
KAPI void nav_grid_agent_commands_register (nav_grid *grid);
