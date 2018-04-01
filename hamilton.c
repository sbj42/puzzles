/*
 * hamilton.c: Implementation for Hamilton Puzzles.
 * (C) 2018 James Clark
 * Created for Simon Tatham's Portable Puzzle Collection
 * See LICENCE for licence details
 *
 * Objective of the game: Construct a number sequence path that fills the grid.
 *
 * This puzzle type is known under several names, including
 * Hidato, Hidoku, Numbrix, and Jadium.
 * 
 * For instance, the puzzle "4x4:,,4,3,,,,,,7,,9,,,,":
 * 
 *   .  .  4  3
 *   .  .  .  .
 *   .  7  .  9
 *   .  .  .  .
 * 
 * Is solved like this:
 * 
 *  16  5  4  3
 *  15  6  1  2
 *  14  7  8  9
 *  13 12 11 10
 *
 * First we generate a random Hamiltonian path.  The method used in
 * this implementation is to start with a basic Hamiltonian path and then
 * shuffle it for a while.  See the comment for the random_hampath() function
 * for implementation details.  The result is a completed solution to the
 * puzzle.
 * 
 * To generate the puzzle itself, we remove numbers from the grid until the
 * desired difficulty is reached while ensuring that the resulting puzzle can
 * still be solved and has only one solution.
 * 
 * Difficulty is determined by which parts of the solver are enabled.  There are
 * two levels of difficulty at the moment:
 * 
 *   Easy: The solution can be obtained only using moves deemed necessary
 *         with a few simple rules and no guess-work.
 *   Hard: The solver may need to make some guesses and see which possibilities
 *         work and which don't.
 * 
 * See the comments for the solver() function for details about which rules
 * are used.
 * 
 * Some variations are permitted by custom configuration:
 * 
 * Diagonal Paths
 * 
 *   Seen in "Hidato" puzzles, this allows the path to travel to diagonally-
 *   adjacent squares.  This makes finding some paths trickier, but the puzzle
 *   usually needs to provide more clues to ensure a single solution.
 * 
 * Include First And Last Clue
 * 
 *   Seen in "Hidato" puzzles, this ensures that the first and last number are
 *   given as clues.  This sometimes makes the puzzle easier, because it removes
 *   all one-sided subpaths, which can be less constrained.
 * 
 * Symmetrical clues
 * 
 *   Seen in most (all?) "Numbrix" puzzles and occasionally in others, this
 *   ensures that the clues given form a two-way rotationally symmetric pattern
 *   on the board.  This is mainly for esthetic effect, and tends to make the
 *   puzzle easier.
 * 
 * Width and Height
 * 
 *   Rectangular shapes are allowed with some restrictions:  We only allow
 *   numbers up to 99, and neither dimension of the puzzle can be less than 3.
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"

/*
 * --- General ---
 * 
 * Types, constants, and functions used by multiple parts of the puzzle code.
 */

typedef unsigned char number_t;  /* stores a number on the grid */
#define NUMBER_MAX 99            /* largest number supported */

typedef unsigned char coord_t;   /* stores an x- or y-coordinate */
#define COORD_MAX 254            /* maximum coordinate value */
#define NO_COORD 255             /* special value for "no coordinate" */

typedef struct {
    coord_t x, y;
} location_t;                    /* specifies a square on the grid using x,y coordinates */

enum {
    DIFF_EASY,        /* easy difficulty - no recursive trial-and-error */
    DIFF_HARD,        /* hard difficulty - recursive trial-and-error allowed */

    DIFF_COUNT        /* number of difficulty levels */
};

/*
 * Returns the "Manhattan" or "taxicab" distance between two locations:
 *   abs(x2 - x1) + abs(y2 - y1)
 */
static int manhattan_distance(int x1, int y1, int x2, int y2) {
    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);
    return dx + dy;
}

/*
 * Returns the "Chebyshev" or "chessboard" distance between two locations:
 *   max(abs(x2 - x1), abs(y2 - y1))
 */
static int chebyshev_distance(int x1, int y1, int x2, int y2) {
    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);
    return max(dx, dy);
}

/*
 * Returns the distance between two locations.  When diagonal is 0, this is
 * uses manhattan_distance, otherwise it uses chebyshev_distance.
 */
static int distance(int x1, int y1, int x2, int y2, int diagonal) {
    if (diagonal) {
        return chebyshev_distance(x1, y1, x2, y2);
    } else {
        return manhattan_distance(x1, y1, x2, y2);
    }
}

/*
 * Writes a location to a particular index in a list of locations and
 * increments the index.
 * 
 * This assumes the list has already been allocated with enough space, it does
 * not resize the list pointer.
 */
static void add_location(location_t * list, int * index, int x, int y) {
    assert(*index >= 0);
    list[*index].x = x;
    list[*index].y = y;
    (*index)++;
}

/*
 * Reverses a list of locations.
 */
static void reverse_locations(location_t * list, int length) {
    location_t temp;
    int a = 0, b = length - 1;
    assert(length > 0);
    while (a < b) {
        /* structure copies: */
        temp = list[a];
        list[a] = list[b];
        list[b] = temp;
        a ++;
        b --;
    }
}

/*
 * Find a location in a list.  Assert if it is not found.
 */
static int find_location(location_t const * list, int length, int x, int y) {
    int index;
    for (index = 0; index < length; index ++) {
        if (list[index].x == x && list[index].y == y)
            return index;
    }
    assert(FALSE);
    return -1;
}

static char * grid_to_string(number_t const * grid, int w, int h)
{
    int linelen, totallen, x, y;
    char *p, *ret;

    /*
     * Line length: we have w numbers, two characters each, and each with
     * a space after it.  The final space is replaced by a newline, but
     * that doesn't affect the length.
     */
    linelen = 3 * w;

    totallen = linelen * h;
    ret = snewn(totallen + 1, char);     /* leave room for terminating NUL */

    p = ret;
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            int n = grid[y * w + x];
            if (n == 0) {
                *p++ = ' ';
                *p++ = '.';
            } else {
                if (n <= 9) {
                    *p++ = ' ';
                } else {
                    int tens = n / 10;
                    *p++ = '0' + tens;
                    n = n - tens * 10;
                }
                *p++ = '0' + n;
            }
            if (x < w - 1)
                *p++ = ' ';
            else
                *p++ = '\n';
        }
    }
    assert(p - ret == totallen);
    *p = '\0';

    return ret;
}

/*
 * --- Random Hamiltonian path generator ---
 * 
 * Start with a basic Hamiltonian path and then shuffle it for a while.
 * See details in the comment for the random_hampath() function.
 */

/*
 * Constructs a simple, winding Hamiltonian path on a rectangular grid.
 * 
 * The path is returned as a dynamically allocated list of locations on the
 * grid.  The length of the list will be (w * h).  It will start with (0, 0).
 * If the height of the grid is odd then the path will end with (width-1,
 * height-1), otherwise it will end with (0, height-1).
 */
static location_t * simple_hampath(int w, int h)
{
    int x, y, p;
    int area = w * h;
    location_t * path = snewn(area, location_t);
    for (y = 0, p = 0; y < h;) {
        /* zig */
        for (x = 0; x < w; x ++)
            add_location(path, &p, x, y);
        y ++;
        if (y == h)
            break;
        /* zag */
        for (x = w-1; x >= 0; x --)
            add_location(path, &p, x, y);
        y ++;
    }
    assert(p == area);
    return path;
}

/*
 * Helper for get_neighbors_except().  Adds a location to the neighbors list if
 * it is not the same as the "except" location.
 */
static void get_neighbors_except_helper(location_t neighbors[7], int * length, int x, int y, location_t const * except)
{
    if (except->x != x || except->y != y)
        add_location(neighbors, length, x, y);
}

/*
 * Builds a list of locations that are adjacent to a given location (cursor),
 * but doesn't include a particular neighbor (except).  There are between 1
 * and 7 such locations, depending on where the cursor is and whether diagonals
 * are considered.  The length of the list is returned.
 */
static int get_neighbors_except(location_t neighbors[7], location_t const * cursor, location_t const * except,
                                int w, int h, int diagonal)
{
    assert(distance(cursor->x, cursor->y, except->x, except->y, diagonal) == 1); /* the two locations must be neighbors */
    int cx = cursor->x;
    int cy = cursor->y;
    int length = 0;
    if (cy - 1 >= 0)
        get_neighbors_except_helper(neighbors, &length, cx, cy - 1, except);
    if (cy + 1 < h)
        get_neighbors_except_helper(neighbors, &length, cx, cy + 1, except);
    if (cx - 1 >= 0)
        get_neighbors_except_helper(neighbors, &length, cx - 1, cy, except);
    if (cx + 1 < w)
        get_neighbors_except_helper(neighbors, &length, cx + 1, cy, except);
    if (diagonal) {
        if (cy - 1 >= 0 && cx - 1 >= 0)
            get_neighbors_except_helper(neighbors, &length, cx - 1, cy - 1, except);
        if (cy + 1 < h && cx - 1 >= 0)
            get_neighbors_except_helper(neighbors, &length, cx - 1, cy + 1, except);
        if (cy - 1 >= 0 && cx + 1 < w)
            get_neighbors_except_helper(neighbors, &length, cx + 1, cy - 1, except);
        if (cy + 1 < h && cx + 1 < w)
            get_neighbors_except_helper(neighbors, &length, cx + 1, cy + 1, except);
    }
    return length;
}

#define SHUFFLE_FACTOR 5       /* scaling factor for how much shuffling to do */

/*
 * Construct a random Hamiltonian path on a rectangular grid.
 * 
 * The path is returned as a dynamically allocated list of locations on the
 * grid.  The length of the list will be (w * h).
 * 
 * This is based on an algorithm apparently described in 'Secondary Structures
 * in Long Compacy Polymers' (https://arxiv.org/abs/cond-mat/0508094).  We
 * start with a simple Hamiltonian path and then "shuffle" it by making random
 * modifications which keep the path Hamiltonian.
 * 
 * The shuffle operation works like this:  Take one end of the path, A.  Find
 * a random neighbor B of A, such that A and B are not directly connected.
 * There is a sequence of nodes in the path from A to B.  In that sequence
 * there is a single node directly connected to B, call that C.  Disconnect B
 * and C, reverse the A..C segment, and connect B and A.
 * 
 * Suppose we start with:
 * 
 *   1  2  3  4
 *   8  7  6  5
 *   9 10 11 12
 *  16 15 14 13
 * 
 * Taking the end of the path labeled "1", we choose a random neighbor.  "2" is
 * already connected to "1".  That leaves "8" (and "7" if diagonals are
 * allowed).  Suppose we choose "8".  Disconnect "8" and "7", reverse "1".."7",
 * and connect "1" and "8".  We get:
 * 
 *   7  6  5  4
 *   8  1  2  3
 *   9 10 11 12
 *  16 15 14 13
 * 
 * For a second shuffle, we can now choose "6", "8" or "10" - or "5", "7", "9",
 * "11" if diagonals are allowed.  If we choose "11" and repeat the procedure,
 * we get:
 * 
 *   4  5  6  7
 *   3 10  9  8
 *   2  1 11 12
 *  16 15 14 13
 * 
 */
static location_t * random_hampath(random_state * rs,
                                   int w, int h, int diagonal)
{
    int area = w * h;
    location_t * path;
    location_t neighbors[7];
    int i;

    /* make a simple Hamiltonian path */
    path = simple_hampath(w, h);

    for (i = 0; i < 2 * SHUFFLE_FACTOR * area; i ++) {
        int neighbors_length;
        /*
         * Due to the random-walk nature of the shuffling, it's possible we
         * will never touch the other end of the path (path[area-1]).  To
         * avoid having too many paths with one end stuck in a corner, we
         * reverse the path halfway through to shuffle the other end.
         */
        if (i == SHUFFLE_FACTOR * area)
            reverse_locations(path, area);

        /*
         * Make a list of all neighbors of (x,y) that are not directly connected
         * to it (i.e. locations that are not the next location in the path).
         */
        neighbors_length = get_neighbors_except(neighbors, &path[0], &path[1], w, h, diagonal);
        assert(neighbors_length > 0);
        assert(diagonal ? neighbors_length < 8 : neighbors_length < 4);

        /* choose a random neighbor */
        int n = random_upto(rs, neighbors_length);

        /* find that neighbor's index in the path */
        int index = find_location(path, area, neighbors[n].x, neighbors[n].y);
        assert(index > 0);

        /* reverse the portion of the path before the neighbor */
        reverse_locations(path, index);
    }

    return path;
}

/*
 * --- Solver ---
 * 
 * Solves hamilton puzzles.
 * 
 * The solver returns the first solution it finds, but it also looks for a
 * second solution.  That's useful to ensure that generated puzzles have only
 * one solution.
 * 
 * We use a set of rules to look for "necessary" moves (i.e. moves that
 * can be deemed necessary without guess-work).  When it cannot find any
 * remaining necessary moves, it uses a recursive trial-and-error technique.
 * 
 * To control the level of difficulty when generating puzzles, we can disable
 * the recursive trial-and-error feature.
 */

/*
 * Computes a map from numbers to locations on the grid.
 * 
 * The map is returned as a dynamically allocated list of locations.  Each
 * location in the list will be the location on the grid of the number
 * corresponding to that index in the location list.  The length of the list
 * is (w * h + 1).  The first location in the list is always (NO_COORD,
 * NO_COORD) because there is never a number 0 on the grid.  Other locations
 * may also be (NO_COORD, NO_COORD), indicating that that number is not present
 * on the grid.
 * 
 * For example, given this grid:
 * 
 *   .  1
 *   3  4
 * 
 * The returned map will have length 5:
 *   map[0] = (NO_COORD, NO_COORD)
 *   map[1] = (1, 0)
 *   map[2] = (NO_COORD, NO_COORD)
 *   map[3] = (0, 1)
 *   map[4] = (1, 1)
 */
static location_t * compute_number_to_location_map(number_t const * grid, int w, int h)
{
    int area = w * h;
    location_t * ret;
    int i, x, y;

    ret = snewn(area + 1, location_t); /* + 1 to accommodate indexes up to and including (area) */

    for (i = 0; i <= area; i ++)
        ret[i].x = ret[i].y = NO_COORD;
        
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x++) {
            int clue = grid[y * w + x];
            if (clue > 0) {
                ret[clue].x = x;
                ret[clue].y = y;
            }
        }
    }
    
    return ret;
}

/*
 * Most of the solver operates on "gaps" between numbers that are on the grid.
 * A gap is a sequence of missing numbers.  Most gaps have two known end
 * locations, and the missing numbers will complete a path from one location
 * to the other.  Sometimes there is a gap at an end of the solution path,
 * in which case the gap has only one anchored location and the other location
 * is unknown.  We'll call that an "open-ended" gap.  
 * 
 * For instance, given this grid:
 * 
 *    .  5  4  3
 *    .  .  1  2
 *   14  .  .  9
 *   13 12 11 10
 * 
 * There are two gaps.  One is between "5" and "9", and the other starts at
 * "14" and goes to the end of the path (16).  These are represented by the
 * following gap structures:
 * 
 *   { n1 = 5, l1 = (1,0), n2 = 9, l2 = (3,2) }
 *   { n1 = 14, l1 = (0,2), n2 = 17, l2 = (NO_COORD, NO_COORD) }
 * 
 */

typedef struct {
    number_t n1;     /* number present on the grid, before the first missing number (0 for an open-ended gap) */
    number_t n2;     /* number present on the grid, after the last missing number (area+1 for an open-ended gap) */
    location_t l1;   /* location of n1 ((NO_COORD, NO_COORD) for an open-ended gap) */
    location_t l2;   /* location of n2 ((NO_COORD, NO_COORD) for an open-ended gap) */
} gap_t;             /* represents a "gap" between two numbers on the grid */

/*
 * Helper function to add gaps to a dynamically allocated gap list.
 */
static void add_gap(gap_t ** gaps, int * gaps_length,
                    number_t n1, coord_t x1, coord_t y1, number_t n2, coord_t x2, coord_t y2)
{
    gap_t * gap;
    *gaps = sresize(*gaps, *gaps_length + 1, gap_t);
    gap = &(*gaps)[*gaps_length];
    gap->n1 = n1;
    gap->n2 = n2;
    gap->l1.x = x1;
    gap->l1.y = y1;
    gap->l2.x = x2;
    gap->l2.y = y2;
    (*gaps_length)++;
}

/*
 * Finds the gaps (sequences of missing numbers) in the given grid.
 * 
 * A gap is represented by two end locations (one of which may be (NO_COORD,
 * NO_COORD)), and two end numbers.  The valid end locations point to squares
 * with the end numbers in them, and the missing numbers are the ones in
 * between.
 * 
 * A gap with (NO_COORD, NO_COORD) at one end is here called "open-ended",
 * and in that case the location of that end of the gap is unknown.  In that
 * case the end number for that location will be an invalid number for the grid,
 * just to keep the property that the missing numbers are the ones in between
 * the end numbers.
 * 
 * For instance, given this grid:
 * 
 *    .  5  4  .
 *    .  .  .  .
 *   14  .  .  9
 *   13 12 11  .
 * 
 * The missing numbers are 1-3, 6-8, 10, and 15-16.  The returned gaps
 * would be:
 *   0 at (NO_COORD, NO_COORD) to 4 at (2, 0)
 *   5 at (1, 0)               to 9 at (3, 2)
 *   9 at (3, 2)               to 11 at (2, 3)
 *   14 at (0, 2)              to 17 at (NO_COORD, NO_COORD)
 * 
 * This function also computes the length of the longest gap (3 in the above
 * example), which is useful for limiting the computational complexity of
 * a generated puzzle.
 */
static void compute_gaps(number_t const * grid, int w, int h,
                         gap_t ** gaps_out, int * gaps_length_out, int * longest_gap_out)
{
    int area = w * h;
    gap_t * gaps = NULL;
    int gaps_length = 0;
    int longest_gap = 0;
    int i, first_number, last_number;

    location_t * number_map = compute_number_to_location_map(grid, w, h);

    /* find the first and last numbers on the grid */
    for (first_number = 1; first_number <= area; first_number ++) {
        if (number_map[first_number].x != NO_COORD)
            break;
    }
    assert(first_number <= area); /* there must be at least one number */
    for (last_number = area; last_number >= first_number; last_number --) {
        if (number_map[last_number].x != NO_COORD)
            break;
    }
    assert(last_number >= first_number);

    /* if the first number is not 1, then the first gap is from 1 to (first_number) */
    if (first_number != 1) {
        location_t * loc = &number_map[first_number];
        add_gap(&gaps, &gaps_length, 0, NO_COORD, NO_COORD, first_number, loc->x, loc->y);
        longest_gap = max(longest_gap, first_number - 1);
    }

    /* add gaps in between numbers */
    for (i = first_number; i <= last_number; i ++) {
        location_t * loc = &number_map[i];
        /* if (i) is present and (i - 1) is not, then we've found the end of the current gap */
        if (i > first_number && loc->x != NO_COORD && number_map[i - 1].x == NO_COORD) {
            gap_t * gap = &gaps[gaps_length - 1];
            gap->n2 = i;
            gap->l2.x = loc->x;
            gap->l2.y = loc->y;
            longest_gap = max(longest_gap, i - gap->n1 - 1);
        }
        /* if (i) is present and (i + 1) is not, then we've found the beginning of the next gap */
        if (i < last_number && loc->x != NO_COORD && number_map[i + 1].x == NO_COORD) {
            add_gap(&gaps, &gaps_length, i, loc->x, loc->y, 0, NO_COORD, NO_COORD);
        }
    }
    
    /* if the last number is not (area), then the last gap is from (last_number) to (area) */
    if (last_number != area) {
        location_t * loc = &number_map[last_number];
        add_gap(&gaps, &gaps_length, last_number, loc->x, loc->y, area + 1, NO_COORD, NO_COORD);
        longest_gap = max(longest_gap, area - last_number);
    }

    sfree(number_map);
    *gaps_out = gaps;
    *gaps_length_out = gaps_length;
    *longest_gap_out = longest_gap;
}

static number_t * dup_grid(number_t const * grid, int area)
{
    number_t * ret = snewn(area, number_t);
    memcpy(ret, grid, area * sizeof(number_t));
    return ret;
}

typedef struct {
    int w, h;              /* grid width and height */
    int diagonal;          /* can the path use diagonal segments */
    int steps_limit;       /* limit how much work is put into solving in recursive mode */
    number_t * grid;       /* puzzle grid */
    gap_t * gaps;          /* list of clue gaps (sequences of missing clues) */
    int gaps_length;       /* number of gaps */
} solver_state_t;

/*
 * Initializes a solver state structure.
 * 
 * This function also computes the length of the longest gap (3 in the above
 * example), which is useful for limiting the computational complexity of
 * a generated puzzle.
 */
static void init_solver_state(solver_state_t * state,
                              number_t const * grid, int w, int h, int diagonal, int steps_limit,
                              int * longest_gap_out)
{
    int area = w * h;
    state->w = w;
    state->h = h;
    state->diagonal = diagonal;
    state->steps_limit = steps_limit;
    state->grid = dup_grid(grid, area);
    compute_gaps(grid, w, h, &state->gaps, &state->gaps_length, longest_gap_out);
}

/*
 * Copies a solver state structure.
 */
static void copy_solver_state(solver_state_t * to_state, solver_state_t const * from_state)
{
    int area = from_state->w * from_state->h;
    to_state->w = from_state->w;
    to_state->h = from_state->h;
    to_state->diagonal = from_state->diagonal;
    to_state->steps_limit = from_state->steps_limit;
    to_state->grid = dup_grid(from_state->grid, area);
    to_state->gaps = snewn(from_state->gaps_length, gap_t);
    memcpy(to_state->gaps, from_state->gaps, from_state->gaps_length * sizeof(gap_t));
    to_state->gaps_length = from_state->gaps_length;
}

/*
 * Closes a solver state structure.
 */
static void close_solver_state(solver_state_t * state)
{
    sfree(state->grid);
    sfree(state->gaps);
}

/*
 * Helper for find_only_move().  If the given location has no number and *nx
 * is NO_COORD, this this sets (*nx,*ny) to that location and returns TRUE.  If
 * the given location has no number and *nx is not NO_COORD, this returns FALSE.
 * Otherwise it returns TRUE.
 * 
 * Basically, the FALSE return signals that there is more than one available
 * square.
 */
static int find_only_move_helper(number_t const * grid, int w, int x, int y, int *nx, int *ny)
{
    if (grid[y * w + x] == 0) {
        if (*nx != NO_COORD)
            return FALSE;
        *nx = x;
        *ny = y;
    }
    return TRUE;
}

/*
 * Looks for moves made necessary because there is only one available square
 * for a number.
 * 
 * Given a target location, this assumes that the number at that location is
 * one end of a gap.  At least one square around that location needs to be
 * filled in with a number.  If there is exactly one available square, then
 * that must be where the number goes.  This function doesn't determine what
 * number goes there, but that can be found later based on which end of the 
 * gap the target location came from.
 * 
 * For example, in the following grid (with no diagonal moves):
 * 
 *    .  5  4  3
 *    .  .  1  2
 *   14  .  8  9
 *    . 12 11  .
 * 
 * There are two necessary moves that can be found by this function.  One
 * next to the "8" at (1,2) and one next to the "9" at (3,3).
 * 
 */
static int find_only_move(number_t const * grid, int w, int h, int diagonal,
                          int x, int y, int *nx, int *ny)
{
    *nx = NO_COORD;
    *ny = NO_COORD;
    if (x > 0 && !find_only_move_helper(grid, w, x - 1, y, nx, ny))
        return FALSE;
    if (x < w - 1 && !find_only_move_helper(grid, w, x + 1, y, nx, ny))
        return FALSE;
    if (y > 0 && !find_only_move_helper(grid, w, x, y - 1, nx, ny))
        return FALSE;
    if (y < h - 1 && !find_only_move_helper(grid, w, x, y + 1, nx, ny))
        return FALSE;
    if (diagonal) {
        if (x > 0 && y > 0 && !find_only_move_helper(grid, w, x - 1, y - 1, nx, ny))
            return FALSE;
        if (x < w - 1 && y > 0 && !find_only_move_helper(grid, w, x + 1, y - 1, nx, ny))
            return FALSE;
        if (x > 0 && y < h - 1 && !find_only_move_helper(grid, w, x - 1, y + 1, nx, ny))
            return FALSE;
        if (x < w - 1 && y < h - 1 && !find_only_move_helper(grid, w, x + 1, y + 1, nx, ny))
            return FALSE;
    }
    /*
     * If we made it this far and *nx != NO_COORD, then there was only one move
     * and it's now stored at (*nx, *ny).
     */
    return *nx != NO_COORD;
}

/*
 * Helper for check_blocked_number().  If the given location has no number
 * or the number is (n - 1) or (n + 1), then available is incremented.
 */
static void check_blocked_number_helper(number_t const * grid, int w,
                                      int x, int y, int n, int *available)
{
    int o = grid[y * w + x];
    if (o == 0 || o == n - 1 || o == n + 1) {
        (*available) ++;
    }
}

/*
 * Checks if the puzzle has been rendered unsolvable because there aren't
 * enough available squares around the given target location.
 * 
 * The numbers 1 and (area - 1) are the ends of the completed path, and so they
 * need to connect to only one available square.  But all others need two.
 * Whenever we place a number (either by determining that it is necessary, or 
 * by guessing during the recursive trial-and-error mode of the solver),
 * we may end up using too many squares around a number.  That makes the puzzle
 * impossible to solve.
 * 
 * For example, in the following grid (with no diagonal moves):
 * 
 *   16 15  .  .
 *   11  .  .  .
 *    .  7  6  .
 *    .  .  .  .
 * 
 * We might try placing an 8 in the position above the 7, but that's a bad
 * move because the 11, which still needs two connections, will have only one
 * available adjacent square.
 * 
 * This test is only ever useful on numbers that have gaps both below them and
 * above them, and that only happens with clues given at the start.  Not with
 * numbers filled in by the solver, because the solver only places numbers
 * connected to numbers already on the board, so placed numbers never require
 * two available squares.
 * 
 */
static int check_blocked_number(number_t const * grid, int w, int h, int diagonal,
                               int x, int y)
{
    int n = grid[y * w + x];
    int available = 0;
    assert(n > 0);
    if (x > 0)
        check_blocked_number_helper(grid, w, x - 1, y, n, &available);
    if (x < w - 1)
        check_blocked_number_helper(grid, w, x + 1, y, n, &available);
    if (y > 0)
        check_blocked_number_helper(grid, w, x, y - 1, n, &available);
    if (y < h - 1)
        check_blocked_number_helper(grid, w, x, y + 1, n, &available);
    if (diagonal) {
        if (x > 0 && y > 0)
            check_blocked_number_helper(grid, w, x - 1, y - 1, n, &available);
        if (x < w - 1 && y > 0)
            check_blocked_number_helper(grid, w, x + 1, y - 1, n, &available);
        if (x > 0 && y < h - 1)
            check_blocked_number_helper(grid, w, x - 1, y + 1, n, &available);
        if (x < w - 1 && y < h - 1)
            check_blocked_number_helper(grid, w, x + 1, y + 1, n, &available);
    }
    return available < 2;
}

/*
 * Checks if the puzzle has been rendered unsolvable because a number
 * placed at the given target location has taken away an available square for
 * a clue nearby.
 * 
 * See the comment on find_blocked_number() for more information about
 * available squares and what it means to block a number.
 * 
 * This looks at the l2 location for each gap, and if that location is adjacent
 * to the given target location then checks to see if it (the l2) is blocked.
 * 
 * We only check one of each gap's end locations because we only need to look at
 * numbers which have gaps on both sides.  Nevertheless, we will still
 * frequently check locations that don't have gaps on both sides, but it's hard
 * to confirm the gaps and harmless to check them anyway.
 */
static int check_blocked_numbers_nearby(solver_state_t const * state, int x, int y)
{
    int g;
    for (g = 0; g < state->gaps_length; g ++) {
        gap_t const * gap = &state->gaps[g];
        if (gap->l2.x != NO_COORD) {
            if (distance(gap->l2.x, gap->l2.y, x, y, state->diagonal) == 1) {
                if (check_blocked_number(state->grid, state->w, state->h, state->diagonal, gap->l2.x, gap->l2.y))
                    return TRUE;
            }
        }
    }
    return FALSE;
}

/*
 * Places a number in a square, and raises the low end of the current gap.
 * 
 * If that makes the puzzle unsolvable, then this function returns FALSE.
 * 
 * If that completes the gap, then the gap is removed.
 */
static int place_number_l1(solver_state_t * state, int gap_index, int x, int y)
{
    int w = state->w;
    gap_t * gap = &state->gaps[gap_index];
    int n = gap->n1 + 1;

    /* if this number would be too far away from the other side of the gap, the puzzle is unsolvable */
    if (gap->l2.x != NO_COORD
        && distance(x, y, gap->l2.x, gap->l2.y, state->diagonal) > gap->n2 - gap->n1 - 1)
        return FALSE;

    state->grid[y * w + x] = n;

    /* if this number blocks another number, the puzzle is unsolvable */
    if (check_blocked_numbers_nearby(state, x, y))
        return FALSE;
    
    if (n + 1 == gap->n2) {
        memmove(&state->gaps[gap_index], &state->gaps[gap_index + 1], sizeof(gap_t) * (state->gaps_length - gap_index - 1));
        state->gaps_length --;
    } else {
        gap->n1 = n;
        gap->l1.x = x;
        gap->l1.y = y;
    }

    return TRUE;
}

/*
 * Places a number in a square, and lowers the high end of the current gap.
 * 
 * If that makes the puzzle unsolvable, then this function returns FALSE.
 * 
 * If that completes the gap, then the gap is removed.
 */
static int place_number_l2(solver_state_t * state, int gap_index, int x, int y)
{
    int w = state->w;
    gap_t * gap = &state->gaps[gap_index];
    int n = gap->n2 - 1;

    /* if this number would be too far away from the other side of the gap, the puzzle is unsolvable */
    if (gap->l1.x != NO_COORD
        && distance(x, y, gap->l1.x, gap->l1.y, state->diagonal) > gap->n2 - gap->n1 - 1)
        return FALSE;

    state->grid[y * w + x] = n;
    
    /* if this number blocks another number, the puzzle is unsolvable */
    if (check_blocked_numbers_nearby(state, x, y))
        return FALSE;

    if (n - 1 == gap->n1) {
        memmove(&state->gaps[gap_index], &state->gaps[gap_index + 1], sizeof(gap_t) * (state->gaps_length - gap_index - 1));
        state->gaps_length --;
    } else {
        gap->n2 = n;
        gap->l2.x = x;
        gap->l2.y = y;
    }

    return TRUE;
}

/* flags returned by the moving functions */
typedef enum {
    UNSOLVABLE,   /* a necessary move was found but it makes the puzzle unsolvable */
    MOVED,        /* a necessary move was found and performed, gaps are updated */
    DIDNT_MOVE    /* no necessary move was found */
} move_result_t;

/*
 * This looks at the ends of a particular gap, to see if either side has
 * a necessary move due to there being only one available square.  If so,
 * it places the necessary number, updates the gap, and checks to see if the
 * puzzle has been made unsolvable.
 * 
 * See the comment on find_only_move() for more information about
 * available squares and how this makes a particular move necessary.
 */
static move_result_t do_only_move(solver_state_t * state, int gap_index)
{
    int w = state->w, h = state->h;
    gap_t const * gap = &state->gaps[gap_index];
    int nx, ny;
    if (gap->l1.x != NO_COORD) {
        if (find_only_move(state->grid, w, h, state->diagonal, gap->l1.x, gap->l1.y, &nx, &ny)) {
            if (place_number_l1(state, gap_index, nx, ny))
                return MOVED;
            else
                return UNSOLVABLE;
        }
    }
    if (gap->l2.x != NO_COORD) {
        if (find_only_move(state->grid, w, h, state->diagonal, gap->l2.x, gap->l2.y, &nx, &ny)) {
            if (place_number_l2(state, gap_index, nx, ny))
                return MOVED;
            else
                return UNSOLVABLE;
        }
    }
    return DIDNT_MOVE;
}

/*
 * This looks for moves made necessary because there is only one possible path
 * that can connect a gap.  If that's the case, it places all the necessary
 * numbers, removes the gap, and checks to see if the puzzle has been made
 * unsolvable.
 * 
 * This finds situations where the two end locations of a gap are so far apart,
 * and the two numbers in those locations so close together, that only a direct
 * path from one to the other will work.
 * 
 * For example, in the following grid (with no diagonal moves):
 * 
 *   10  .  .  7
 *    . 12  .  .
 *   16  .  2  .
 *    . 14  .  .
 * 
 * There are three gaps that can be completed by this function.  One is
 * from "7" to "10", and the other is from "12" to "14".
 * 
 * This function is only applicable for paths that are not open-ended (that is,
 * neither end location can be (NO_COORD, NO_COORD)).
 */
static move_result_t do_straight_path(solver_state_t * state, int gap_index)
{
    int w = state->w;
    int x, y, sx, sy, n;
    gap_t const * gap = &state->gaps[gap_index];
    if (state->diagonal) {
        int dx = gap->l2.x - gap->l1.x;
        int dy = gap->l2.y - gap->l1.y;
        int adx = abs(dx);
        if (dx == dy || dx == -dy) {
            if (gap->n2 - gap->n1 != adx)
                return DIDNT_MOVE;
        } else
            return DIDNT_MOVE;
        sx = dx > 0 ? 1 : -1;
        sy = dy > 0 ? 1 : -1;
    } else {
        if (gap->l1.x == gap->l2.x) {
            if (gap->n2 - gap->n1 != abs(gap->l2.y - gap->l1.y))
                return DIDNT_MOVE;
            sx = 0;
            sy = gap->l2.y - gap->l1.y > 0 ? 1 : -1;
        } else if (gap->l1.y == gap->l2.y) {
            if (gap->n2 - gap->n1 != abs(gap->l2.x - gap->l1.x))
                return DIDNT_MOVE;
            sx = gap->l2.x - gap->l1.x > 0 ? 1 : -1;
            sy = 0;
        } else
            return DIDNT_MOVE;
    }
    x = gap->l1.x;
    y = gap->l1.y;
    for (n = gap->n1 + 1; n < gap->n2; n ++) {
        x += sx;
        y += sy;
        /* if there's already a number in this square, the puzzle is unsolvable */
        if (state->grid[y * w + x] != 0)
            return UNSOLVABLE;
        state->grid[y * w + x] = n;
        /* if this number blocks another number, the puzzle is unsolvable */
        if (check_blocked_numbers_nearby(state, x, y))
            return UNSOLVABLE;
    }
    memmove(&state->gaps[gap_index], &state->gaps[gap_index + 1], sizeof(gap_t) * (state->gaps_length - gap_index - 1));
    state->gaps_length --;
    return MOVED;
}

/*
 * This implements part of the solver algorithm: it looks for moves that it
 * can determine are necessary, and plays those moves until it can prove that
 * the puzzle is unsolvable, or until it can't find any more necessary moves.
 * 
 * There are two kinds of necessary moves we look for:
 * 
 *   do_straight_path() looks for gaps that can only be completed with a direct
 *   path.
 *
 *   do_only_move() looks for gap ends that have only one available move.
 * 
 * Whenever a move is made, a gap is reduced or removed.
 * 
 * If this function can determine that the puzzle is unsolvable it will return
 * FALSE.  Returning TRUE doesn't necessarily mean the puzzle is solvable,
 * although if this function returns TRUE and removes all of the gaps, then
 * the puzzle has been solved.
 * 
 * If there are still gaps after this returns, then to continue solving the
 * puzzle we will need to try recursive trial-and-error.
 */
static int do_necessary_moves(solver_state_t * state)
{
    int g, changed;
    do {
        changed = FALSE;
        for (g = 0; g < state->gaps_length; g++) {
            switch (do_straight_path(state, g)) {
                case UNSOLVABLE: return FALSE;
                case MOVED:
                    changed = TRUE;
                    g --;
                    continue;
                default: break; /* placate compiler warning */
            }
            switch (do_only_move(state, g)) {
                case UNSOLVABLE: return FALSE;
                case MOVED:
                    changed = TRUE;
                    g --;
                    continue;
                default: break; /* placate compiler warning */
            }
        }
    } while (changed);

    return TRUE;
}

/* forward declaration */
static int do_recursive_solve(solver_state_t * state, number_t ** solution, int * multiple, int * steps);

static int do_recursive_solve_step_l1(solver_state_t const * state, int gap_index, int x, int y,
                                      number_t ** solution, int * multiple, int * steps)
{
    int w = state->w;
    solver_state_t next_state;
    int finished = FALSE;
    if (state->grid[y * w + x] != 0)
        return FALSE;
    copy_solver_state(&next_state, state);
    if (place_number_l1(&next_state, gap_index, x, y))
        finished = do_recursive_solve(&next_state, solution, multiple, steps);
    close_solver_state(&next_state);
    return finished;
}

static int do_recursive_solve_step_l2(solver_state_t const * state, int gap_index, int x, int y,
                                      number_t ** solution, int * multiple, int * steps)
{
    int w = state->w;
    solver_state_t next_state;
    int finished = FALSE;
    if (state->grid[y * w + x] != 0)
        return FALSE;
    copy_solver_state(&next_state, state);
    if (place_number_l2(&next_state, gap_index, x, y))
        finished = do_recursive_solve(&next_state, solution, multiple, steps);
    close_solver_state(&next_state);
    return finished;
}

/*
 * This function first calls do_necessary_moves() to try to make some progress
 * on the puzzle solution without recursion.  If the puzzle is still unsolved
 * after that, it uses a recursive trial-and-error technique.  It looks at one
 * end location of one gap.  For every possible move from that location, it
 * creates a new solver state, makes that move, and then calls itself on
 * the resulting state.
 * 
 * If the puzzle is solved, then the solution is saved as a dynamically
 * allocated grid.  If the caller wants to know if there are multiple solutions
 * (e.g. when generating a puzzle), it should pass a non-NULL pointer for
 * the "multiple" argument.  The value pointed to by multiple will be set to
 * TRUE if there are multiple soultions.
 * 
 * This function returns TRUE when the solver is finished.  That is, if
 * multiple is NULL and steps_limit is not greater than 0, then this returns
 * TRUE when asolution is found or when all moves are tried.  If multiple is
 * not NULL, then this returns TRUE when two solutions are found or when all
 * moves are tried.  If steps_limit is greater than 0, then the solver may
 * return TRUE before trying all moves, if the puzzle requires too many
 * recursive steps to solve.
 * 
 * The caller must initialize solution to NULL and multiple to FALSE before
 * calling.
 */
static int do_recursive_solve(solver_state_t * state, number_t ** solution, int * multiple, int * steps)
{
    int w = state->w, h = state->h, area = w * h;
    gap_t const * gap;
    if (!do_necessary_moves(state))
        return FALSE;
    if (state->steps_limit > 0 && (*steps) ++ > state->steps_limit)
        return TRUE;
    if (state->gaps_length == 0) {
        if (*solution) {
            assert(multiple);
            *multiple = TRUE;
            return TRUE;
        }
        *solution = dup_grid(state->grid, area);
        return multiple == NULL;
    }
    gap = &state->gaps[0];
    if (gap->l1.x != NO_COORD) {
        int x = gap->l1.x, y = gap->l1.y;
        if (x > 0 && do_recursive_solve_step_l1(state, 0, x - 1, y, solution, multiple, steps))
            return TRUE;
        if (y > 0 && do_recursive_solve_step_l1(state, 0, x, y - 1, solution, multiple, steps))
            return TRUE;
        if (x < w - 1 && do_recursive_solve_step_l1(state, 0, x + 1, y, solution, multiple, steps))
            return TRUE;
        if (y < h - 1 && do_recursive_solve_step_l1(state, 0, x, y + 1, solution, multiple, steps))
            return TRUE;
        if (state->diagonal) {
            if (x > 0 && y > 0 && do_recursive_solve_step_l1(state, 0, x - 1, y - 1, solution, multiple, steps))
                return TRUE;
            if (x > 0 && y < h - 1 && do_recursive_solve_step_l1(state, 0, x - 1, y + 1, solution, multiple, steps))
                return TRUE;
            if (x < w - 1 && y > 0 && do_recursive_solve_step_l1(state, 0, x + 1, y - 1, solution, multiple, steps))
                return TRUE;
            if (x < w - 1 && y < h - 1 && do_recursive_solve_step_l1(state, 0, x + 1, y + 1, solution, multiple, steps))
                return TRUE;
        }
    } else {
        int x = gap->l2.x, y = gap->l2.y;
        if (x > 0 && do_recursive_solve_step_l2(state, 0, x - 1, y, solution, multiple, steps))
            return TRUE;
        if (y > 0 && do_recursive_solve_step_l2(state, 0, x, y - 1, solution, multiple, steps))
            return TRUE;
        if (x < w - 1 && do_recursive_solve_step_l2(state, 0, x + 1, y, solution, multiple, steps))
            return TRUE;
        if (y < h - 1 && do_recursive_solve_step_l2(state, 0, x, y + 1, solution, multiple, steps))
            return TRUE;
        if (state->diagonal) {
            if (x > 0 && y > 0 && do_recursive_solve_step_l2(state, 0, x - 1, y - 1, solution, multiple, steps))
                return TRUE;
            if (x > 0 && y < h - 1 && do_recursive_solve_step_l2(state, 0, x - 1, y + 1, solution, multiple, steps))
                return TRUE;
            if (x < w - 1 && y > 0 && do_recursive_solve_step_l2(state, 0, x + 1, y - 1, solution, multiple, steps))
                return TRUE;
            if (x < w - 1 && y < h - 1 && do_recursive_solve_step_l2(state, 0, x + 1, y + 1, solution, multiple, steps))
                return TRUE;
        }
    }
    return FALSE;
}

static int gap_compare(const void *av, const void *bv)
{
    const gap_t *a = (const gap_t *)av, *b = (const gap_t *)bv;
    int dista = manhattan_distance(a->l1.x, a->l1.y, a->l2.x, a->l2.y);
    int distb = manhattan_distance(b->l1.x, b->l1.y, b->l2.x, b->l2.y);
    return dista - distb;
}

static int gap_compare_diag(const void *av, const void *bv)
{
    const gap_t *a = (const gap_t *)av, *b = (const gap_t *)bv;
    int dista = chebyshev_distance(a->l1.x, a->l1.y, a->l2.x, a->l2.y);
    int distb = chebyshev_distance(b->l1.x, b->l1.y, b->l2.x, b->l2.y);
    return dista - distb;
}

/*
 * Hamilton puzzle solver.
 * 
 * Given a puzzle and some solver settings, this tries to find a solution.  If
 * found, the solution is saved as a new dynamically allocated grid, and TRUE
 * is returned.  FALSE may indicate that there is no solution, or just that
 * the solver was not able to find it, depending on the settings:
 * 
 *   max_gap_length: Convenience setting for the puzzle generator.  Tells
 *                   the solver to return FALSE early if there is a number gap
 *                   longer than this number, even if the puzzle is solvable.
 *                   Set this to -1 to accept any gap length.
 * 
 *   max_difficulty: If this is set to DIFF_EASY, then the recursive trial-and-
 *                   error mode of the solver is disabled.  That means the
 *                   solver will only be able to find a solution by looking at
 *                   "necessary" moves.  Set this to -1 to accept any
 *                   difficulty.
 * 
 *   steps_limit: If this is greater than 0, then the recursive trial-and-
 *                error mode of the solver will only tolerate the given number
 *                of nodes in the recursion tree.  This basically limits how
 *                much work is put into finding the solution.  Set this to -1
 *                to indicate no limit.
 * 
 *   unique_only: If TRUE, the solver will keep looking after finding a
 *                solution, to see if there is more than one possible solution.
 *                If multiple solutions are found, the solver will return FALSE.
 */
static int solver(number_t const * grid, int w, int h, int diagonal,
                  int max_gap_length, int max_difficulty, int steps_limit, int unique_only,
                  number_t ** solution)
{
    int area = w * h;
    solver_state_t state;
    int longest_gap_length;

    *solution = NULL;
    init_solver_state(&state, grid, w, h, diagonal, steps_limit, &longest_gap_length);
    if (max_gap_length > 0 && longest_gap_length > max_gap_length)
        return FALSE;

    if (max_difficulty == DIFF_EASY) {
        if (do_necessary_moves(&state) && state.gaps_length == 0) {
            *solution = dup_grid(state.grid, area);
            return TRUE;
        }
    } else {
        number_t * rsolution = NULL;
        int multiple = FALSE;
        int steps = 0;

        /* sort the gaps by length, faster that way */
        qsort(state.gaps, state.gaps_length, sizeof(gap_t), diagonal ? gap_compare_diag : gap_compare);

        do_recursive_solve(&state, &rsolution, unique_only ? &multiple : NULL, &steps);
        if (multiple) {
            sfree(rsolution);
            return FALSE;
        } else if (rsolution) {
            *solution = rsolution;
            return TRUE;
        }
    }
    return FALSE;
}

/*
 * --- Game parameters ---
 * 
 * Parameter and configuration types and functions required to implement the
 * puzzle back end.
 */

#define SIDE_MIN 2        /* minimum size for each dimension of the grid */

enum {
    PATT_NONE,        /* random asymmetric */
    PATT_ROT2,        /* random 2-way rotational symmetry */
    PATT_RING,        /* ring one square away from the edge */
    PATT_BORDER       /* every other border square */
};

struct game_params {
    int w, h;              /* grid width and height */
    int diagonal;          /* can the path use diagonal segments */
    int keep_ends;         /* first and last clue stay */
    int pattern;           /* pattern */
    int difficulty;        /* difficulty */
};

static void default_params(game_params * params)
{
    params->w = params->h = 7;
    params->diagonal = FALSE;
    params->keep_ends = FALSE;
    params->pattern = PATT_ROT2;
    params->difficulty = DIFF_EASY;
}

/*
 * Constructs default parameters: 9x9, easy, symmetrical.
 */
static game_params * game_default_params(void)
{
    game_params * ret = snew(game_params);

    default_params(ret);

    return ret;
}

/*
 * Clones a game parameters structure.
 */
static game_params * game_dup_params(game_params const * params)
{
    game_params * ret = snew(game_params);

    *ret = *params;  /* structure copy */

    return ret;
}

/*
 * Frees a game parameters structure.
 */
static void game_free_params(game_params * params)
{
    sfree(params);
}

/*
 * Validates a game parameters structure.
 */
static const char * game_validate_params(const game_params *params, int full)
{
    if (params->w < SIDE_MIN || params->h < SIDE_MIN)
	    return "Both dimensions must be at least "STR(SIDE_MIN);
    if (params->w > COORD_MAX || params->h > COORD_MAX)
	    return "Dimensions greater than "STR(COORD_MAX)" are not supported";
    if ((params->w * params->h) > NUMBER_MAX)
        return "Unable to support more than "STR(NUMBER_MAX)" distinct symbols in a puzzle";
    if (params->difficulty < 0 || params->difficulty >= DIFF_COUNT)
        return "Unknown difficulty rating";
    return NULL;
}

/*
 * Decodes a game parameters string.
 */
static void game_decode_params(game_params * params, char const * string)
{
    default_params(params);

    /* First number is treated as width and height */
    params->w = params->h = atoi(string);
    while (*string && isdigit((unsigned char)*string)) string ++;

    /* Independent width and height are separated by 'x' */
    if (*string == 'x') {
        string ++;
        params->h = atoi(string);
	    while (*string && isdigit((unsigned char)*string)) string ++;
    }
    while (*string) {
        char c = *string++;
        if (c == 'o') {
            /* o for "ordinal directions" */
            params->diagonal = TRUE;
        } else if (c == 'k') {
            params->keep_ends = TRUE;
        } else if (c == 'p') {
            c = *string++;
            if (c) {
                if (c == 'a') {
                    params->pattern = PATT_NONE;
                } else if (c == '2') {
                    params->pattern = PATT_ROT2;
                } else if (c == 'r') {
                    params->pattern = PATT_RING;
                } else if (c == 'b') {
                    params->pattern = PATT_BORDER;
                }
            }
        } else if (c == 'd') {
            c = *string++;
            if (c) {
                if (c == 'e') {
                    params->difficulty = DIFF_EASY;
                } else if (c == 'h') {
                    params->difficulty = DIFF_HARD;
                }
            }
        }
    }
}

/*
 * Encodes a game parameters string.
 */
static char * game_encode_params(game_params const * params, int full)
{
    char str[80];

    sprintf(str, "%dx%d", params->w, params->h);
    if (params->diagonal)
	    strcat(str, "o");
    /* The following parameters only affect generation of the puzzle: */
    if (full) {
        if (params->keep_ends)
            strcat(str, "k");
        switch (params->pattern) {
            case PATT_NONE: strcat(str, "pa"); break;
            /* case PATT_ROT2: strcat(str, "p2"); break; [default] */
            case PATT_RING: strcat(str, "pr"); break;
            case PATT_BORDER: strcat(str, "pb"); break;
        }
        switch (params->difficulty) {
            /* case DIFF_EASY: strcat(str, "de"); break; [default] */
            case DIFF_HARD: strcat(str, "dh"); break;
        }
    }
    
    return dupstr(str);
}

/*
 * Preset parameters
 */
struct preset {
    const char *title;
    game_params params;
};

static struct preset const presets[] = {
    { "7x7 Easy", { 7, 7, FALSE, FALSE, PATT_ROT2, DIFF_EASY } },
    { "7x7 Ring", { 7, 7, FALSE, FALSE, PATT_RING, DIFF_HARD } },
    { "7x7 Border", { 7, 7, FALSE, FALSE, PATT_BORDER, DIFF_HARD } },
    { "7x7 Hard", { 7, 7, FALSE, FALSE, PATT_ROT2, DIFF_HARD } },
    { "9x9 Easy", { 9, 9, FALSE, FALSE, PATT_ROT2, DIFF_EASY } },
    { "9x9 Hard", { 9, 9, FALSE, FALSE, PATT_ROT2, DIFF_HARD } },
};

/*
 * Returns preset parameters
 */
static int game_fetch_preset(int i, char ** name, game_params ** params)
{
    if (i < 0 || i >= lenof(presets))
        return FALSE;

    *name = dupstr(presets[i].title);
    *params = game_dup_params(&presets[i].params);

    return TRUE;
}

/*
 * This puzzle has custom configuration options
 */
enum {
    game_can_configure = TRUE
};

enum {
    CONFIG_WIDTH,          /* grid width */
    CONFIG_HEIGHT,         /* grid height */
    CONFIG_DIAGONAL,       /* can the path use diagonal segments */
    CONFIG_KEEP_ENDS,      /* first and last clue stay */
    CONFIG_PATTERN,    /* clue pattern */
    CONFIG_DIFFICULTY,     /* difficulty */

    CONFIG_COUNT           /* number of configuration items */
};

/*
 * Returns items for custom configuration
 */
static config_item * game_configure(game_params const * params)
{
    config_item * ret;
    char buf[80];

    ret = snewn(CONFIG_COUNT + 1, config_item);  /* + 1 for the end-of-list marker */

    ret[CONFIG_WIDTH].name = "Grid width";
    ret[CONFIG_WIDTH].type = C_STRING;
    sprintf(buf, "%d", params->w);
    ret[CONFIG_WIDTH].u.string.sval = dupstr(buf);

    ret[CONFIG_HEIGHT].name = "Grid height";
    ret[CONFIG_HEIGHT].type = C_STRING;
    sprintf(buf, "%d", params->h);
    ret[CONFIG_HEIGHT].u.string.sval = dupstr(buf);

    ret[CONFIG_DIAGONAL].name = "Use diagonal path segments";
    ret[CONFIG_DIAGONAL].type = C_BOOLEAN;
    ret[CONFIG_DIAGONAL].u.boolean.bval = params->diagonal;

    ret[CONFIG_KEEP_ENDS].name = "Keep first and last clue";
    ret[CONFIG_KEEP_ENDS].type = C_BOOLEAN;
    ret[CONFIG_KEEP_ENDS].u.boolean.bval = params->keep_ends;

    ret[CONFIG_PATTERN].name = "Clue Pattern";
    ret[CONFIG_PATTERN].type = C_CHOICES;
    ret[CONFIG_PATTERN].u.choices.choicenames = ":Random:Random Symmetrical:Ring:Border";
    ret[CONFIG_PATTERN].u.choices.selected = params->pattern;

    ret[CONFIG_DIFFICULTY].name = "Difficulty";
    ret[CONFIG_DIFFICULTY].type = C_CHOICES;
    ret[CONFIG_DIFFICULTY].u.choices.choicenames = ":Easy:Hard";
    ret[CONFIG_DIFFICULTY].u.choices.selected = params->difficulty;

    ret[CONFIG_COUNT].name = NULL;
    ret[CONFIG_COUNT].type = C_END;

    return ret;
}

/*
 * Creates a game parameters structure from custom configuration items
 */
static game_params * game_custom_params(config_item const * cfg)
{
    game_params * ret = snew(game_params);

    ret->w = atoi(cfg[CONFIG_WIDTH].u.string.sval);
    ret->h = atoi(cfg[CONFIG_HEIGHT].u.string.sval);
    ret->diagonal = cfg[CONFIG_DIAGONAL].u.boolean.bval;
    ret->keep_ends = cfg[CONFIG_KEEP_ENDS].u.boolean.bval;
    ret->pattern = cfg[CONFIG_PATTERN].u.choices.selected;
    ret->difficulty = cfg[CONFIG_DIFFICULTY].u.choices.selected;

    return ret;
}

/*
 * --- Generator ---
 * 
 * Generates hamilton puzzles.
 * 
 */

#define MAX_GAP_LENGTH 9

static number_t * path_to_grid(location_t const * path, int w, int h)
{
    int area = w * h;
    number_t * ret = snewn(area, number_t);
    int i;
    for (i = 0; i < area; i ++) {
        int x = path[i].x;
        int y = path[i].y;
        ret[y * w + x] = i + 1;
    }
    return ret;
}

/*
 * Constructs a new random puzzle.
 * 
 * We start with a random Hamiltonian path, which will be the solution to
 * the puzzle.  Then we go through the clues in random order and try to remove
 * them.  If the puzzle does not have a unique solution after removing a clue,
 * then we put the clue back and keep trying.
 * 
 * This returns the grid of clues, as a list of numbers.  0 indicates no clue
 * given for that square.  The list will have (params->w * params->h) numbers.
 * 
 */
static number_t * generate_puzzle(game_params const * params, random_state * rs)
{
    int w = params->w, h = params->h, area = w*h;
    location_t * path;
    number_t * grid;
    int max_gap_length = MAX_GAP_LENGTH;
    int steps_limit = -1;
    int difficulty = params->difficulty;
    int x, y, i;

    if (params->diagonal) {
        /*
         * Diagonal puzzles take more time to solve.  Reduce the solver effort
         * limit so it doesn't take too long to generate a puzzle.
         */
        steps_limit = 80000;
        if (params->pattern == PATT_RING)
            steps_limit = 1000;
        if (params->pattern == PATT_BORDER)
            steps_limit = 100;
    } else {
        if (params->pattern == PATT_NONE)
            steps_limit = 300000;
        if (params->pattern == PATT_ROT2)
            steps_limit = 800000;
    }

    while (TRUE) {

        /* generate a random path */
        path = random_hampath(rs, w, h, params->diagonal);

        /* convert that into a grid of numbers */
        grid = path_to_grid(path, w, h);

        if (params->pattern == PATT_RING) {
            number_t * solution;
            for (y = 0; y < h; y ++) {
                for (x = 0; x < w; x ++) {
                    if (x == 0 || x == w - 1 || y == 0 || y == h - 1
                        || (x != 1 && x != w - 2 && y != 1 && y != h - 2))
                        grid[y * w + x] = 0;
                }
            }
            
            if (solver(grid, w, h, params->diagonal, max_gap_length, difficulty, steps_limit, TRUE, &solution)) {
                sfree(solution);
                break;
            }
        } else if (params->pattern == PATT_BORDER) {
            number_t * solution;
            for (y = 0; y < h; y ++) {
                for (x = 0; x < w; x ++) {
                    if ((x != 0 && x != w - 1 && y != 0 && y != h - 1)
                        || ((x + y) & 1) == 1)
                        grid[y * w + x] = 0;
                }
            }
            
            max_gap_length = max(w,h) + (params->difficulty == DIFF_HARD ? 4 : 0);
            difficulty = DIFF_HARD;

            if (solver(grid, w, h, params->diagonal, max_gap_length, difficulty, steps_limit, TRUE, &solution)) {
                sfree(solution);
                break;
            }
        } else {
            number_t * clues;
            int clues_length;

            /* make a shuffled list of clues to remove */
            clues = dup_grid(grid, area);
            clues_length = area;
            if (params->pattern == PATT_ROT2) {
                /* for symmetrical clue patterns, we consider only the clues in the first half of the grid */
                clues_length = (area + 1) / 2;
            }
            shuffle(clues, clues_length, sizeof(number_t), rs);

            for (i = 0; i < clues_length; i ++) {
                /* try removing a clue from the grid, see if it can still be solved */
                number_t * solution;
                int clue = clues[i];
                int sclue = 0;
                int rx = path[clue - 1].x;
                int ry = path[clue - 1].y;
                assert(grid[ry * w + rx] == clue);

                /* keep_ends tells us to keep the first and last clue */
                if (params->keep_ends && (clue == 1 || clue == area))
                    continue;

                if (params->pattern == PATT_ROT2) {
                    /* For symmetrical clue patterns we always remove clues in symmetrical pairs */
                    sclue = grid[(h - 1 - ry) * w + (w - 1 - rx)];
                    if (params->keep_ends && (sclue == 1 || sclue == area))
                        continue;
                    grid[(h - 1 - ry) * w + (w - 1 - rx)] = 0;
                }

                grid[ry * w + rx] = 0;

                if (solver(grid, w, h, params->diagonal, max_gap_length, difficulty, steps_limit, TRUE, &solution)) {
                    sfree(solution);
                } else {
                    /* no solution, restore the clues */
                    grid[ry * w + rx] = clue;
                    if (params->pattern == PATT_ROT2) {
                        grid[(h - 1 - ry) * w + (w - 1 - rx)] = sclue;
                    }
                }
            }

            sfree(clues);
            break;
        }
    }

    sfree(path);
    return grid;
}

/*
 * --- Game description ---
 * 
 * Game description functions required to implement the puzzle back end.
 */

/*
 * Saves the given generated puzzle grid as a "game description",
 * a dynamically allocated string that can be used to recreate the same puzzle
 * later.
 */
static char * encode_desc_grid(number_t const * grid, int w, int h)
{
    int area = w * h;
    int len;
    char * p, * ret;
    int x, y;

    /*
     * Length: we have [area] numbers, two characters each, and each with
     * a comma after it.  The last comma will be replaced by a terminating NUL.
     */
    len = 3 * area;
    ret = snewn(len, char);

    p = ret;
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            if (x != 0 || y != 0)
                *p++ = ',';
            int d = grid[y * w + x];
            if (d != 0) {
                if (d > 9) {
                    int tens = d / 10;
                    *p++ = '0' + tens;
                    d = d - tens * 10;
                }
                *p++ = '0' + d;
            }
        }
    }
    assert(p - ret <= len);
    *p = '\0';

    return ret;
}

/*
 * Parses a "game description" as a puzzle grid.
 */
static number_t * decode_desc_grid(const char * desc, int w, int h)
{
    int area = w * h;
    number_t * grid = snewn(area, number_t);
    int index = 0;
    grid[index] = 0;
    while (*desc) {
        char c = *desc++; 
        if (c == ',') {
            index ++;
            grid[index] = 0;
        } else if (c > '0' && c <= '9') {
            int clue = atoi(desc - 1);
            grid[index] = clue;
            while (*desc >= '0' && *desc <= '9')
                desc++;
        }
    }
    return grid;
}

/*
 * Generates a new puzzle and returns it encoded as a game description.
 */
static char * game_new_desc(game_params const * params, random_state * rs,
			                char ** aux, int interactive)
{
    int w = params->w, h = params->h;
    number_t * grid = generate_puzzle(params, rs);
    char * ret;

    ret = encode_desc_grid(grid, w, h);

    sfree(grid);
    return ret;
}

/*
 * Validates a game description.
 */
static char const * game_validate_desc(game_params const * params, char const * desc)
{
    int area = params->w * params->h;
    int squares = 1;

    while (*desc) {
        char c = *desc++; 
        if (c == ',') {
            squares++;
            continue;
        } else if (c < '0' || c > '9')
            return "Invalid character in game description";
    }
    if (squares < area)
        return "Not enough data to fill grid";
    if (squares > area)
        return "Too much data to fit in grid";
    return NULL;
}

/*
 * --- Game state ---
 * 
 * Game state functions required to implement the puzzle back end.
 */

enum {
    LINE_N = 0x01,
    LINE_E = 0x02,
    LINE_S = 0x04,
    LINE_W = 0x08,
    LINE_NE = 0x10,
    LINE_SE = 0x20,
    LINE_SW = 0x40,
    LINE_NW = 0x80,
};

typedef struct {
    location_t l;             /* the location of the number on the grid */
} number_info_t;

typedef struct {
    unsigned char is_clue;    /* the number was given as a clue, and cannot be changed */
    unsigned char is_bad;     /* the number should be highlighted as having a problem */
    unsigned char lines;      /* lines into and out of this square */
} square_info_t;

struct game_state {
    int w, h;
    int diagonal;
    number_t * grid;               /* the current puzzle grid */
    number_info_t * number_infos;
    square_info_t * square_infos;

    int completed, cheated;
};

/*
 * Helper for update_lines().  This returns TRUE if the square at (x,y) contains 
 * the next number up or down from (n).
 */
static int update_lines_helper(number_t const * grid, int w, int x, int y, int n)
{
    int neighbor = grid[y * w + x];
    return neighbor && (neighbor == n - 1 || neighbor == n + 1);
}

/*
 * Sets the lines flags for the given square.
 */
static void update_lines(const game_state * state, int x, int y)
{
    int w = state->w, h = state->h;
    int n = state->grid[y * w + x];
    unsigned char lines = 0;
    if (n) {
        if (x > 0 && update_lines_helper(state->grid, w, x - 1, y, n))
            lines |= LINE_W;
        if (x < w - 1 && update_lines_helper(state->grid, w, x + 1, y, n))
            lines |= LINE_E;
        if (y > 0 && update_lines_helper(state->grid, w, x, y - 1, n))
            lines |= LINE_N;
        if (y < h - 1 && update_lines_helper(state->grid, w, x, y + 1, n))
            lines |= LINE_S;
        if (state->diagonal) {
            if (x > 0 && y > 0 && update_lines_helper(state->grid, w, x - 1, y - 1, n))
                lines |= LINE_NW;
            if (x > 0 && y < h - 1 && update_lines_helper(state->grid, w, x - 1, y + 1, n))
                lines |= LINE_SW;
            if (x < w - 1 && y > 0 && update_lines_helper(state->grid, w, x + 1, y - 1, n))
                lines |= LINE_NE;
            if (x < w - 1 && y < h - 1 && update_lines_helper(state->grid, w, x + 1, y + 1, n))
                lines |= LINE_SE;
        }
    }
    state->square_infos[y * w + x].lines = lines;
}

/*
 * Determines whether a square should be marked as "bad".  Two squares are 
 * considered bad when they contain sequential numbers but are not adjacent.
 * 
 * For instance, in the following grid:
 * 
 * 10  .  .  .
 *  . 12  .  6
 * 16  .  2  7
 *  .  .  9  8
 * 
 * The squares with "10" and "9" are bad.
 */
static int is_bad_square(game_state const * state, int x, int y, int n)
{
    int w = state->w, h = state->h, area = w * h;
    if (n == 0)
        return FALSE;
    if (n > 1) {
        number_info_t const * on = &state->number_infos[n - 1];
        if (on->l.x != NO_COORD && distance(x, y, on->l.x, on->l.y, state->diagonal) != 1)
            return TRUE;
    }
    if (n < area) {
        number_info_t const * on = &state->number_infos[n + 1];
        if (on->l.x != NO_COORD && distance(x, y, on->l.x, on->l.y, state->diagonal) != 1)
            return TRUE;
    }
    return FALSE;
}

/*
 * Start a new game by creating a new game state structure from a game
 * description string.
 */
static game_state * game_new_game(midend * me, game_params const * params,
                                  char const * desc)
{
    int w = params->w, h = params->h, area = w * h;
    game_state * state = snew(game_state);
    int x, y, n;

    state->w = params->w;
    state->h = params->h;
    state->diagonal = params->diagonal;
    state->grid = decode_desc_grid(desc, w, h);
    state->square_infos = snewn(area, square_info_t);
    state->number_infos = snewn(area + 1, number_info_t);
    state->completed = state->cheated = FALSE;

    memset(state->square_infos, 0, sizeof(square_info_t) * area);
    for (n = 1; n <= area; n ++) {
        state->number_infos[n].l.x = state->number_infos[n].l.y = NO_COORD;
    }
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            n = state->grid[y * w + x];
            if (n > 0) {
                state->number_infos[n].l.x = x;
                state->number_infos[n].l.y = y;
                state->square_infos[y * w + x].is_clue = TRUE;
                update_lines(state, x, y);
            }
        }
    }

    return state;
}

/*
 * Clones a game state structure.
 */
static game_state * game_dup_game(game_state const * state)
{
    int w = state->w, h = state->h, area = w * h;
    game_state * ret = snew(game_state);

    ret->w = state->w;
    ret->h = state->h;
    ret->diagonal = state->diagonal;
    ret->grid = dup_grid(state->grid, area);
    ret->square_infos = snewn(area, square_info_t);
    memcpy(ret->square_infos, state->square_infos, sizeof(square_info_t) * area);
    ret->number_infos = snewn(area + 1, number_info_t);
    memcpy(ret->number_infos, state->number_infos, sizeof(number_info_t) * (area + 1));
    ret->completed = state->completed;
    ret->cheated = state->cheated;

    return ret;
}

/*
 * Frees a game state structure.
 */
static void game_free_game(game_state * state)
{
    sfree(state->grid);
    sfree(state->square_infos);
    sfree(state->number_infos);
    sfree(state);
}

/*
 * Parses a move string and executes the move.
 * 
 * Three kinds of move strings are supported:
 * 
 *   "A" x "," y ":" n
 *     Places the number (n) at the location (x,y)
 *   "R" x "," y
 *     Removes the number at the location (x,y)
 *   "S" desc_grid
 *     For the solve command, this rewrites the grid completely
 */
static game_state * game_execute_move(game_state const * state, char const * move)
{
    game_state * ret = game_dup_game(state);
    int w = state->w, h = state->h, area = w * h;
    int i, x, y, n;

	if (sscanf(move, "A%d,%d:%d", &x, &y, &n) == 3) {
        ret->grid[y * w + x] = n;

    } else if (sscanf(move, "R%d,%d", &x, &y) == 2) {
        ret->grid[y * w + x] = 0;

    } else if (move[0] == 'S') {
        sfree(ret->grid);
        ret->grid = decode_desc_grid(&move[1], w, h);
        ret->cheated = TRUE;
    }

    /* recompute number and square info */
    ret->completed = TRUE;
    for (i = 1; i <= area; i ++) {
        ret->number_infos[i].l.x = ret->number_infos[i].l.y = NO_COORD;
    }
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            n = ret->grid[y * w + x];
            if (n > 0) {
                ret->number_infos[n].l.x = x;
                ret->number_infos[n].l.y = y;
            }
            if (n == 0)
                ret->completed = FALSE;
        }
    }
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            n = ret->grid[y * w + x];
            int is_bad = is_bad_square(ret, x, y, n);
            ret->square_infos[y * w + x].is_bad = is_bad;
            if (is_bad)
                ret->completed = FALSE;
            update_lines(ret, x, y);
        }
    }

    return ret;
}

enum {
    game_can_solve = TRUE
};

static char * game_solve(game_state const * state, game_state const * currstate,
                         char const * aux, char const ** error)
{
    int w = state->w, h = state->h;
    number_t * solution;
    int steps_limit = 1000000;

    if (solver(state->grid, w, h, state->diagonal, -1, -1, steps_limit, FALSE, &solution)) {
        /*
         * Generate a move string for the solution, which is just the letter
         * "S" followed by an encoded solution grid.
         */
        char * desc = encode_desc_grid(solution, w, h);
        char * move = snewn(strlen(desc) + 2, char);
        move[0] = 'S';
        memcpy(&move[1], desc, strlen(desc) + 1);
        sfree(solution);
        sfree(desc);
        return move;
    }

    *error = "Cannot find a solution";
    return NULL;
}

enum {
    game_can_format_as_text_ever = TRUE
};

static int game_can_format_as_text_now(game_params const * params)
{
    return TRUE;
}

static char * game_text_format(game_state const * state)
{
    return grid_to_string(state->grid, state->w, state->h);
}

/*
 * --- Game UI ---
 * 
 * Game UI functions required to implement the back end.
 */

struct game_ui {
    location_t highlight;   /* highlighted square */
    int next;               /* next number */
    int dir;                /* direction for next number */
};

/*
 * Create a new game ui structure.
 */
static game_ui * game_new_ui(game_state const * state)
{
    game_ui * ui = snew(game_ui);

    ui->highlight.x = ui->highlight.y = NO_COORD;
    ui->dir = 0;
    ui->next = 0;

    return ui;
}

/*
 * Free a game ui structure.
 */
static void game_free_ui(game_ui * ui)
{
    sfree(ui);
}

/*
 * Save important parts of the ui structure to a string.
 */
static char * game_encode_ui(game_ui const * ui)
{
    /* no need for this puzzle */
    return NULL;
}

/*
 * Restore important parts of the ui structure from a string.
 */
static void game_decode_ui(game_ui * ui, char const * encoding)
{
    /* no need for this puzzle */
}

/*
 * Update the ui structure when game state changes.
 */
static void game_changed_state(game_ui * ui, game_state const * oldstate,
                               game_state const * newstate)
{
    /* no need for this puzzle */
}

/*
 * --- Graphics ---
 */

typedef struct {
    number_t n;               /* the number drawn on the square */
    unsigned char highlight;  /* whether this square is highlighted */
    unsigned char is_bad;     /* whether the number is marked as bad */
    unsigned char lines;      /* lines drawn into and out of this square */
} square_draw_info_t;

struct game_drawstate {
    square_draw_info_t * grid;    /* the grid as currently drawn on the screen */
    int next;                     /* number displayed as next */
    int tilesize;
};

/*
 * The game is mainly drawn as a simple grid, but below the grid there is space
 * for text that says what the "next" number might be.  The next number, if
 * present, is either before or after the number in the highlighted square.  If
 * the user clicks a square adjacent to the highlighted square, the next number
 * is what will be placed there.
 * 
 * Around the grid and next number is some outer padding.  The next number is
 * placed in an area with the width of the grid and the height of one square.
 * The squares have thin gridlines between and around them.
 * 
 */

enum {
    game_preferred_tilesize = 42
};

#define OUTER_PADDING (tilesize / 2)
#define GRID_LINE_THICKNESS 1
#define NEXT_HEIGHT (tilesize)
#define N_TILES_SIZE(n) ((GRID_LINE_THICKNESS + tilesize) * n + GRID_LINE_THICKNESS)
#define GAME_WIDTH(w) (OUTER_PADDING + N_TILES_SIZE(w) + OUTER_PADDING)
#define GAME_HEIGHT(h) (OUTER_PADDING + N_TILES_SIZE(h) + NEXT_HEIGHT + OUTER_PADDING)

/* derived metrics for the font size and path line */
#define PATH_LINE_THICKNESS (tilesize / 5)
#define FONT_SIZE (tilesize / 2)

/*
 * Creates a new drawstate structure.
 */
static game_drawstate * game_new_drawstate(drawing * dr, game_state const * state)
{
    game_drawstate * ds = snew(game_drawstate);

    ds->grid = NULL; /* this NULL indicates that the game hasn't been drawn yet */
    ds->tilesize = ds->next = 0;

    return ds;
}

/*
 * Frees a drawstate structure.
 */
static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->grid);
    sfree(ds);
}

/*
 * Given a tile size, compute the size of the drawing area
 */
static void game_compute_size(game_params const * params, int tilesize,
                              int * x, int * y)
{
    *x = GAME_WIDTH(params->w);
    *y = GAME_HEIGHT(params->h);
}

/*
 * Prepare to draw at the given tile size.
 */
static void game_set_size(drawing * dr, game_drawstate * ds,
                          game_params const * params, int tilesize)
{
    ds->tilesize = tilesize;
}

enum {
    COL_BACKGROUND,
    COL_GRID,          /* the color of the grid lines */
    COL_CLUE,          /* the foreground color of a number given as a clue */
    COL_USER,          /* the foreground color of a number added by the user */
    COL_HIGHLIGHT,     /* the background color of the highlighted square */
    COL_ERROR,         /* the foreground color of a number in a "bad" square */
    COL_NEXT,          /* the color of the next-number text */
    COL_LINE,          /* the color of the path lines */
    NCOLOURS
};

static void set_color(float * color, float r, float g, float b) {
    color[0] = r;
    color[1] = g;
    color[2] = b;
}

/*
 * Generates colors for the drawing routines.
 */
static float * game_colours(frontend * fe, int * ncolours)
{
    float * ret = snewn(3 * NCOLOURS, float);

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    set_color(&ret[COL_GRID * 3], 0.0F, 0.0F, 0.0F);
    set_color(&ret[COL_CLUE * 3], 0.0F, 0.0F, 0.0F);
    set_color(&ret[COL_USER * 3], 0.0F, 0.6F, 0.0F);
    set_color(&ret[COL_HIGHLIGHT * 3],
        0.78F * ret[COL_BACKGROUND * 3 + 0],
        0.78F * ret[COL_BACKGROUND * 3 + 1],
        0.78F * ret[COL_BACKGROUND * 3 + 2]);
    set_color(&ret[COL_ERROR * 3], 1.0F, 0.0F, 0.0F);
    set_color(&ret[COL_NEXT * 3], 0.0F, 0.0F, 0.0F);
    set_color(&ret[COL_LINE * 3],
        0.8F * ret[COL_BACKGROUND * 3 + 0],
        0.8F * ret[COL_BACKGROUND * 3 + 1],
        1.0F * ret[COL_BACKGROUND * 3 + 2]);

    *ncolours = NCOLOURS;
    return ret;
}

/*
 * Draws the square at (x,y).
 * 
 * If flash is TRUE, the square is highlighted, even if (ui->highlight) is not
 * pointing at the square.  This is used to flash all squares when the game is
 * completed.
 */
static void draw_tile(drawing * dr, game_drawstate * ds,
                      game_state const * state,
                      game_ui const * ui,
                      int x, int y, int flash)
{
    int tilesize = ds->tilesize;
    int i = y * state->w + x;
    int n = state->grid[i];
    square_info_t const * square = &state->square_infos[i];
    int highlight = x == ui->highlight.x && y == ui->highlight.y;

    int square_bg = flash || highlight ? COL_HIGHLIGHT : COL_BACKGROUND;
    int number_fg = square->is_bad ? COL_ERROR : square->is_clue ? COL_CLUE : COL_USER;

    int tx1 = OUTER_PADDING + N_TILES_SIZE(x);
    int ty1 = OUTER_PADDING + N_TILES_SIZE(y);
    int cx = tx1 + tilesize / 2;
    int cy = ty1 + tilesize / 2;
    
    clip(dr, tx1, ty1, tilesize, tilesize);
    draw_rect(dr, tx1, ty1, tilesize, tilesize, square_bg);

    if (square->lines) {
        float fx = (float)cx;
        float fy = (float)cy;
        float t = (float)PATH_LINE_THICKNESS;
        int lines = square->lines;
        draw_circle(dr, cx, cy, PATH_LINE_THICKNESS / 2, COL_LINE, COL_LINE);
        if ((lines & LINE_W) != 0)
            draw_thick_line(dr, t, fx, fy, fx - tilesize / 2, fy, COL_LINE);
        if ((lines & LINE_E) != 0)
            draw_thick_line(dr, t, fx, fy, fx + tilesize / 2, fy, COL_LINE);
        if ((lines & LINE_N) != 0)
            draw_thick_line(dr, t, fx, fy, fx, fy - tilesize / 2, COL_LINE);
        if ((lines & LINE_S) != 0)
            draw_thick_line(dr, t, fx, fy, fx, fy + tilesize / 2, COL_LINE);
        if ((lines & LINE_NW) != 0)
            draw_thick_line(dr, t, fx, fy, fx - tilesize / 2, fy - tilesize / 2, COL_LINE);
        if ((lines & LINE_SE) != 0)
            draw_thick_line(dr, t, fx, fy, fx + tilesize / 2, fy + tilesize / 2, COL_LINE);
        if ((lines & LINE_NE) != 0)
            draw_thick_line(dr, t, fx, fy, fx + tilesize / 2, fy - tilesize / 2, COL_LINE);
        if ((lines & LINE_SW) != 0)
            draw_thick_line(dr, t, fx, fy, fx - tilesize / 2, fy + tilesize / 2, COL_LINE);
    }

    if (n > 0) {
        char buf[80];
        sprintf(buf, "%d", n);
        draw_text(dr, cx, cy, FONT_VARIABLE, FONT_SIZE, ALIGN_VCENTRE | ALIGN_HCENTRE, number_fg, buf);
    }

    unclip(dr);
    draw_update(dr, tx1, ty1, tilesize, tilesize);

    ds->grid[i].n = n;
    ds->grid[i].highlight = highlight;
    ds->grid[i].is_bad = square->is_bad;
    ds->grid[i].lines = square->lines;
}

/*
 * Draws the next-number text.
 */
static void draw_next(drawing * dr, game_drawstate * ds,
                      game_state const * state,
                      game_ui const * ui)
{
    int tilesize = ds->tilesize;
    int w = state->w, h = state->h;
    int n = ui->next;

    int rx1 = OUTER_PADDING;
    int ry1 = OUTER_PADDING + N_TILES_SIZE(h);
    int rw = N_TILES_SIZE(w);
    int rh = tilesize;

    clip(dr, rx1, ry1, rw, rh);
    draw_rect(dr, rx1, ry1, rw, rh, COL_BACKGROUND);

    if (n > 0) {
        int cy = ry1 + tilesize / 2;
        char buf[80];
        assert(ui->next <= NUMBER_MAX);
        sprintf(buf, "next: %d", ui->next);
        draw_text(dr, rx1, cy, FONT_VARIABLE, FONT_SIZE, ALIGN_VCENTRE | ALIGN_HLEFT, COL_NEXT, buf);
    }
    
    unclip(dr);
    draw_update(dr, rx1, ry1, rw, rh);

    ds->next = n;
}

#define FLASH_FRAME 0.12F              /* duration of each flash and the time between flashes */
#define FLASH_TIME (FLASH_FRAME * 4)   /* 4 frames: on, off, on, off */

/*
 * Draws the game window.
 */
static void game_redraw(drawing * dr, game_drawstate * ds,
                        game_state const * oldstate, game_state const * state,
                        int dir, game_ui const * ui,
                        float animtime, float flashtime)
{
    int tilesize = ds->tilesize;
    int w = state->w, h = state->h, area = w * h;
    int x, y;

    if (ds->grid == NULL) {
        int ow = GAME_WIDTH(w);
        int oh = GAME_HEIGHT(h);

        draw_rect(dr, 0, 0, ow, oh, COL_BACKGROUND);
        draw_rect(dr, OUTER_PADDING, OUTER_PADDING, N_TILES_SIZE(w), N_TILES_SIZE(h), COL_GRID);

        /* first time drawing, allocate the grid */
        ds->grid = snewn(area, square_draw_info_t);

        for (y = 0; y < h; y ++) {
            for (x = 0; x < w; x ++) {
                draw_tile(dr, ds, state, ui, x, y, FALSE);
            }
        }
        draw_next(dr, ds, state, ui);

        /* update everything */
        draw_update(dr, 0, 0, ow, oh);
    } else {

        /* alternate flashes */
        int flashing = flashtime > 0;
        int flash = flashing && ((int)(flashtime / FLASH_FRAME) & 1) == 0;

        for (y = 0; y < h; y ++) {
            for (x = 0; x < w; x ++) {
                int i = y * w + x;
                int highlight = x == ui->highlight.x && y == ui->highlight.y;
                square_info_t const * square = &state->square_infos[i];
                square_draw_info_t const * dsquare = &ds->grid[i];
                if (flashing
                    || dsquare->highlight != highlight
                    || dsquare->is_bad != square->is_bad
                    || dsquare->lines != square->lines
                    || dsquare->n != state->grid[i])
                    draw_tile(dr, ds, state, ui, x, y, flash);
            }
        }
        if (ds->next != ui->next)
            draw_next(dr, ds, state, ui);
    }
}

static float game_anim_length(game_state const * oldstate,
                              game_state const * newstate, int dir, game_ui * ui)
{
    /* no animations */
    return 0.0F;
}

static float game_flash_length(game_state const * oldstate,
                               game_state const * newstate, int dir, game_ui * ui)
{
    /* when the game is completed without cheating, flash */
    if (!oldstate->completed && newstate->completed && !newstate->cheated)
        return FLASH_TIME;
    return 0.0F;
}

/*
 * Indicates if the game has been won.
 */
static int game_status(game_state const * state)
{
    return state->completed ? 1 : 0;
}

enum {
    game_is_timed = FALSE
};

/*
 * Indicates if the timer should be running.
 */
static int game_timing_state(game_state const * state, game_ui * ui)
{
    if (state->completed)
	    return FALSE;
    return TRUE;
}

enum {
    game_can_print = TRUE,
    game_can_print_in_colour = FALSE
};

static void game_print_size(game_params const * params, float * x, float * y)
{
    int pw, ph;

    game_compute_size(params, 900, &pw, &ph);
    *x = pw / 100.0F;
    *y = ph / 100.0F;
}

static void game_print(drawing * dr, game_state const * state, int tilesize)
{
    int w = state->w, h = state->h;
    int black = print_mono_colour(dr, 0);
    int grey = print_grey_colour(dr, 0.80F);
    int x, y;

    /* thick outline */
    print_line_width(dr, tilesize * 3 / 40);
    draw_rect_outline(dr, OUTER_PADDING, OUTER_PADDING, N_TILES_SIZE(w), N_TILES_SIZE(h), black);
    
    /* inner grid lines */
    for (x = 1; x < w; x++) {
        print_line_width(dr, tilesize / 40);
        draw_line(dr, OUTER_PADDING + N_TILES_SIZE(x), OUTER_PADDING,
            OUTER_PADDING + N_TILES_SIZE(x), OUTER_PADDING + N_TILES_SIZE(h), black);
    }
    for (y = 1; y < h; y++) {
        print_line_width(dr, tilesize / 40);
        draw_line(dr, OUTER_PADDING, OUTER_PADDING + N_TILES_SIZE(y),
            OUTER_PADDING + N_TILES_SIZE(w), OUTER_PADDING + N_TILES_SIZE(y), black);
    }

    /* clues */
    print_line_width(dr, PATH_LINE_THICKNESS);
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int n = state->grid[y * w + x];
            if (n > 0) {
                int cx = OUTER_PADDING + N_TILES_SIZE(x) + tilesize/2;
                int cy = OUTER_PADDING + N_TILES_SIZE(y) + tilesize/2;
                int lines;
                char str[80];
                sprintf(str, "%d", n);
                /* lines */
                update_lines(state, x, y);
                lines = state->square_infos[y * w + x].lines;
                if (lines) {
                    draw_circle(dr, cx, cy, PATH_LINE_THICKNESS / 4, grey, grey);
                    if ((lines & LINE_W) != 0)
                        draw_line(dr, cx, cy, cx - tilesize / 2, cy, grey);
                    if ((lines & LINE_E) != 0)
                        draw_line(dr, cx, cy, cx + tilesize / 2, cy, grey);
                    if ((lines & LINE_N) != 0)
                        draw_line(dr, cx, cy, cx, cy - tilesize / 2, grey);
                    if ((lines & LINE_S) != 0)
                        draw_line(dr, cx, cy, cx, cy + tilesize / 2, grey);
                    if ((lines & LINE_NW) != 0)
                        draw_line(dr, cx, cy, cx - tilesize / 2, cy - tilesize / 2, grey);
                    if ((lines & LINE_SE) != 0)
                        draw_line(dr, cx, cy, cx + tilesize / 2, cy + tilesize / 2, grey);
                    if ((lines & LINE_NE) != 0)
                        draw_line(dr, cx, cy, cx + tilesize / 2, cy - tilesize / 2, grey);
                    if ((lines & LINE_SW) != 0)
                        draw_line(dr, cx, cy, cx - tilesize / 2, cy + tilesize / 2, grey);
                }

                /* clue number */
                draw_text(dr, cx, cy, FONT_VARIABLE, tilesize/2,
                    ALIGN_VCENTRE | ALIGN_HCENTRE, black, str);
            }
        }
    }
}

enum {
    game_wants_statusbar = FALSE,
    game_flags = 0
};

/*
 * --- User interaction ---
 */

/*
 * Encodes a user interaction as a move string.
 * 
 * 
 */
static char * game_interpret_move(game_state const * state, game_ui * ui,
                             game_drawstate const * ds,
                             int x, int y, int button)
{
    int tilesize = ds->tilesize;
    int w = state->w, h = state->h, area = w * h;

    int tx = (x - OUTER_PADDING) / (tilesize + GRID_LINE_THICKNESS);
    int ty = (y - OUTER_PADDING) / (tilesize + GRID_LINE_THICKNESS);

    if (tx >= 0 && tx < w && ty >= 0 && ty < h) {
        int i = ty * w + tx;
        square_info_t const * square = &state->square_infos[i];
        int n = state->grid[i];

        /* right-click on a number that isn't a clue to remove it */
        if (button == RIGHT_BUTTON && n > 0 && !square->is_clue) {
            char buf[80];
            sprintf(buf, "R%d,%d", tx, ty);
            /* clear the highlight and next number */
            ui->next = ui->dir = 0;
            ui->highlight.x = ui->highlight.y = NO_COORD;
            return dupstr(buf);
        }

        if (button == LEFT_BUTTON) {
            if (n > 0) {
                number_info_t const * n_before = n > 1 ? &state->number_infos[n - 1] : NULL;
                number_info_t const * n_after = n < area ? &state->number_infos[n + 1] : NULL;
                int dir = 0;
                if (ui->highlight.x == tx && ui->highlight.y == ty) {

                    /*
                     * Left-clicked on an already-highlighted number.  This can
                     * be for changing the direction of the next number, or for
                     * removing the number.
                     */

                    /* if next is n+1 and n-1 is not yet on the grid, set next to n-1 */
                    if (ui->next == n + 1 && n_before && n_before->l.x == NO_COORD)
                        dir = -1;
                    /* else if the square is a clue and next is n-1 and n+1 is not yet on the grid, set next to n+1 */
                    else if (square->is_clue && ui->next == n - 1 && n_after && n_after->l.x == NO_COORD)
                        dir = 1;
                    /* else if the square is a clue, don't change next (there's only one direction we can go) */
                    else if (square->is_clue)
                        dir = ui->dir;
                    else {
                        char buf[80];
                        /* remove the number */
                        sprintf(buf, "R%d,%d", tx, ty);
                        /* if n-1 is on the grid and adjacent move the highlight to there */
                        if (n_before && n_before->l.x != NO_COORD && distance(tx, ty, n_before->l.x, n_before->l.y, state->diagonal) == 1) {
                            ui->highlight.x = n_before->l.x;
                            ui->highlight.y = n_before->l.y;
                            ui->dir = 1;
                            ui->next = n;
                        /* else if n+1 is on the grid and adjacent move the highlight to there */
                        } else if (n_after && n_after->l.x != NO_COORD && distance(tx, ty, n_after->l.x, n_after->l.y, state->diagonal) == 1) {
                            ui->highlight.x = n_after->l.x;
                            ui->highlight.y = n_after->l.y;
                            ui->dir = -1;
                            ui->next = n;
                        /* else no highlight */
                        } else {
                            ui->next = ui->dir = 0;
                            ui->highlight.x = ui->highlight.y = NO_COORD;
                        }
                        return dupstr(buf);
                    }
                } else {
                    
                    /*
                     * Left-clicked on an non-highlighted number.  We highlight
                     * it, and then set the next number based on which numbers
                     * are available.
                     */

                    /* if n+1 is available, that's the next number */
                    if (n_after && n_after->l.x == NO_COORD) {
                        dir = 1;
                    /* else if n-1 is available, that's the next number */
                    } else if (n_before && n_before->l.x == NO_COORD) {
                        dir = -1;
                    /* else no next number */
                    } else {
                        dir = 0;
                    }
                }
                ui->highlight.x = tx;
                ui->highlight.y = ty;
                ui->dir = dir;
                ui->next = dir ? n + dir : 0;
                return UI_UPDATE;
            } else {

                /*
                 * Left-clicked on an empty square.  If there is a next number
                 * and the clicked square is adjacent to the highlighted
                 * square, then place the next number there.
                 */
                
                if (ui->next > 0 && distance(tx, ty, ui->highlight.x, ui->highlight.y, state->diagonal) == 1) {
                    char buf[80];
                    sprintf(buf, "A%d,%d:%d", tx, ty, ui->next);

                    /* highlight the clicked square */
                    ui->highlight.x = tx;
                    ui->highlight.y = ty;

                    /* if that was a "bad" move, then clear the next number */
                    if (is_bad_square(state, tx, ty, ui->next)) {
                        ui->next = ui->dir = 0;
                    } else {
                        /*
                         * If the path keeps going from here, have the highlight
                         * follow the path until the next gap.
                         */
                        while (1) {
                            number_info_t const * n_next;
                            ui->next += ui->dir;
                            /* if the path is complete all the way to 1 or area, clear the highlight and next number */
                            if (ui->next > area || ui->next < 1) {
                                ui->next = ui->dir = 0;
                                ui->highlight.x = ui->highlight.y = NO_COORD;
                                break;
                            }
                            n_next = &state->number_infos[ui->next];
                            /* stop if we've found a gap */
                            if (n_next->l.x == NO_COORD)
                                break;
                            /* the path keeps going, move the highlight and keep following */
                            ui->highlight.x = n_next->l.x;
                            ui->highlight.y = n_next->l.y;
                        }
                    }
                    return dupstr(buf);
                } else {
                    /*
                     * Clicked on an empty square, but not to put a number
                     * there, so just clear the highlight and next number.
                     */
                    ui->next = ui->dir = 0;
                    ui->highlight.x = ui->highlight.y = -1;
                    return UI_UPDATE;
                }
            }
        }
    }
    return NULL;
}

#ifdef COMBINED
#define thegame hamilton
#endif

const struct game thegame = {
    "Hamilton",           /* name */
    "games.hamilton",     /* winhelp_topic */
    "hamilton",           /* htmlhelp_topic */
    game_default_params,
    game_fetch_preset,
    NULL,                 /* preset_menu */
    game_decode_params,
    game_encode_params,
    game_free_params,
    game_dup_params,
    game_can_configure,
    game_configure,
    game_custom_params,
    game_validate_params,
    game_new_desc,
    game_validate_desc,
    game_new_game,
    game_dup_game,
    game_free_game,
    game_can_solve, game_solve,
    game_can_format_as_text_ever, game_can_format_as_text_now, game_text_format,
    game_new_ui,
    game_free_ui,
    game_encode_ui,
    game_decode_ui,
    game_changed_state,
    game_interpret_move,
    game_execute_move,
    game_preferred_tilesize, game_compute_size, game_set_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    game_status,
    game_can_print, game_can_print_in_colour, game_print_size, game_print,
    game_wants_statusbar,
    game_is_timed, game_timing_state,
    game_flags,				       /* flags */
};

#ifdef TESTING

#include <time.h>

static void test_random_hampath(game_params const * params, random_state * rs)
{
    location_t * path;
    number_t * grid;
    char * str;
    
    path = random_hampath(rs, params->w, params->h, params->diagonal);
    grid = path_to_grid(path, params->w, params->h);
    str = grid_to_string(grid, params->w, params->h);
    printf(str);
}

static double test_generate(game_params const * params, random_state * rs)
{
    number_t * grid;
    char * str;
    clock_t t1;
    double ret;

    t1 = clock();
    grid = generate_puzzle(params, rs);
    str = grid_to_string(grid, params->w, params->h);
    ret = 1000.0*(clock()-t1)/CLOCKS_PER_SEC;
    printf(str);
    printf("time: %.2f ms\n", ret);
    return ret;
}

double test(random_state * rs)
{
    game_params * params;

    params = game_default_params();

    params->w = params->h = 9;
    params->diagonal = FALSE;
    params->keep_ends = FALSE;
    params->pattern = PATT_ROT2;

    //test_random_hampath(params, random_copy(rs));

    //params->difficulty = DIFF_EASY;
    //test_generate(params, random_copy(rs));

    params->difficulty = DIFF_HARD;
    return test_generate(params, random_copy(rs));
}

int main(int argc, char **argv)
{
    double totaldur = 0;
    double maxdur = 0;
    int count = 10;
    for (int i = 0; i < count; i ++) {
        double dur = test(random_new((char*)&i, sizeof(i)));
        totaldur += dur;
        maxdur = max(maxdur,dur);
    }
    printf("%f ms/puzzle, %f max\n", totaldur / count, maxdur);

    return 0;
}

#endif
