#include "nav_grid_tests.h"

#include <defines.h>
#include <memory/kmemory.h>
#include <world/nav_grid.h>

#include "../expect.h"
#include "../test_manager.h"

static u8 nav_grid_straight_line (void) {
	nav_grid *grid = nav_grid_create(8, 8, 1.0f, 0.0f, 0.0f);
	u32 count = 0;
	nav_cell *path = nav_grid_find_path(grid, (nav_cell){0, 0}, (nav_cell){5, 0}, true, &count);
	expect_should_be(6, count);
	for (u32 i = 0; i < count; ++i) {
		expect_should_be((i32)i, path[i].x);
		expect_should_be(0, path[i].y);
	}
	kfree(path);
	nav_grid_destroy(grid);
	return true;
}

static u8 nav_grid_goes_around_a_wall (void) {
	nav_grid *grid = nav_grid_create(7, 5, 1.0f, 0.0f, 0.0f);
	nav_grid_blocked_rect_set(grid, 3, 0, 1, 4, true); // a wall with a gap at the bottom
	u32 count = 0;
	nav_cell *path = nav_grid_find_path(grid, (nav_cell){0, 0}, (nav_cell){6, 0}, false, &count);
	expect_should_be(15, count); // down 4, across 6, up 4, plus the start
	for (u32 i = 0; i < count; ++i) {
		expect_to_be_true(!nav_grid_blocked_get(grid, path[i].x, path[i].y));
	}
	expect_should_be(6, path[count - 1].x);
	kfree(path);
	nav_grid_destroy(grid);
	return true;
}

static u8 nav_grid_reports_no_path (void) {
	nav_grid *grid = nav_grid_create(5, 5, 1.0f, 0.0f, 0.0f);
	nav_grid_blocked_rect_set(grid, 2, 0, 1, 5, true); // a wall across the whole grid
	u32 count = 99;
	expect_should_be(0, (u64)nav_grid_find_path(grid, (nav_cell){0, 0}, (nav_cell){4, 4}, true, &count));
	expect_should_be(0, count);
	// A blocked or outside end has no path either.
	expect_should_be(0, (u64)nav_grid_find_path(grid, (nav_cell){0, 0}, (nav_cell){2, 2}, true, &count));
	expect_should_be(0, (u64)nav_grid_find_path(grid, (nav_cell){0, 0}, (nav_cell){9, 9}, true, &count));
	nav_grid_destroy(grid);
	return true;
}

static u8 nav_grid_does_not_cut_corners (void) {
	nav_grid *grid = nav_grid_create(3, 3, 1.0f, 0.0f, 0.0f);
	nav_grid_blocked_set(grid, 1, 0, true);
	nav_grid_blocked_set(grid, 0, 1, true);
	u32 count = 0;
	// (0,0) is boxed in except for the diagonal past two blocked corners.
	expect_should_be(0, (u64)nav_grid_find_path(grid, (nav_cell){0, 0}, (nav_cell){2, 2}, true, &count));
	nav_grid_destroy(grid);
	return true;
}

static u8 nav_grid_prefers_diagonals_when_allowed (void) {
	nav_grid *grid = nav_grid_create(6, 6, 1.0f, 0.0f, 0.0f);
	u32 diagonal_count = 0;
	u32 straight_count = 0;
	nav_cell *diagonal = nav_grid_find_path(grid, (nav_cell){0, 0}, (nav_cell){4, 4}, true, &diagonal_count);
	nav_cell *straight = nav_grid_find_path(grid, (nav_cell){0, 0}, (nav_cell){4, 4}, false, &straight_count);
	expect_should_be(5, diagonal_count);
	expect_should_be(9, straight_count);
	kfree(diagonal);
	kfree(straight);
	nav_grid_destroy(grid);
	return true;
}

// Many equally cheap paths exist across an open grid; the answer must still be one fixed path.
static u8 nav_grid_ties_are_broken_the_same_way_every_time (void) {
	nav_grid *grid = nav_grid_create(9, 9, 1.0f, 0.0f, 0.0f);
	u32 first_count = 0;
	nav_cell *first = nav_grid_find_path(grid, (nav_cell){0, 0}, (nav_cell){8, 5}, true, &first_count);
	for (u32 run = 0; run < 5; ++run) {
		u32 count = 0;
		nav_cell *again = nav_grid_find_path(grid, (nav_cell){0, 0}, (nav_cell){8, 5}, true, &count);
		expect_should_be(first_count, count);
		for (u32 i = 0; i < count; ++i) {
			expect_should_be(first[i].x, again[i].x);
			expect_should_be(first[i].y, again[i].y);
		}
		kfree(again);
	}
	expect_should_be(9, first_count); // 5 diagonals and 3 straights, plus the start
	kfree(first);
	nav_grid_destroy(grid);
	return true;
}

static u8 nav_grid_converts_world_positions (void) {
	nav_grid *grid = nav_grid_create(10, 10, 2.0f, -10.0f, -10.0f);
	nav_cell cell = nav_grid_cell_from_world(grid, -9.0f, -7.5f);
	expect_should_be(0, cell.x);
	expect_should_be(1, cell.y);
	cell = nav_grid_cell_from_world(grid, -10.5f, 0.0f);
	expect_should_be(-1, cell.x); // left of the grid
	f32 x = 0;
	f32 y = 0;
	nav_grid_cell_centre(grid, (nav_cell){0, 1}, &x, &y);
	expect_float_to_be(-9.0f, x);
	expect_float_to_be(-7.0f, y);
	nav_grid_destroy(grid);
	return true;
}

void nav_grid_register_tests (void) {
	test_manager_register_test(nav_grid_straight_line, "nav grid: a straight path on an open grid.");
	test_manager_register_test(nav_grid_goes_around_a_wall, "nav grid: the path goes around a wall through the gap.");
	test_manager_register_test(nav_grid_reports_no_path, "nav grid: blocked, outside and unreachable ends have no path.");
	test_manager_register_test(nav_grid_does_not_cut_corners, "nav grid: a diagonal step does not pass a blocked corner.");
	test_manager_register_test(nav_grid_prefers_diagonals_when_allowed, "nav grid: diagonals are shorter, and can be turned off.");
	test_manager_register_test(nav_grid_ties_are_broken_the_same_way_every_time, "nav grid: equal-cost paths give the same answer every run.");
	test_manager_register_test(nav_grid_converts_world_positions, "nav grid: world positions convert to cells and back.");
}
