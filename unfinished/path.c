/*
 * Experimental grid generator for Nikoli's `Number Link' puzzle.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "puzzles.h"

/*
 * 2005-07-08: This is currently a Path grid generator which will
 * construct valid grids at a plausible speed. However, the grids
 * are not of suitable quality to be used directly as puzzles.
 * 
 * The basic strategy is to start with an empty grid, and
 * repeatedly either (a) add a new path to it, or (b) extend one
 * end of a path by one square in some direction and push other
 * paths into new shapes in the process. The effect of this is that
 * we are able to construct a set of paths which between them fill
 * the entire grid.
 * 
 * Quality issues: if we set the main loop to do (a) where possible
 * and (b) only where necessary, we end up with a grid containing a
 * few too many small paths, which therefore doesn't make for an
 * interesting puzzle. If we reverse the priority so that we do (b)
 * where possible and (a) only where necessary, we end up with some
 * staggeringly interwoven grids with very very few separate paths,
 * but the result of this is that there's invariably a solution
 * other than the intended one which leaves many grid squares
 * unfilled. There's also a separate problem which is that many
 * grids have really boring and obvious paths in them, such as the
 * entire bottom row of the grid being taken up by a single path.
 * 
 * It's not impossible that a few tweaks might eliminate or reduce
 * the incidence of boring paths, and might also find a happy
 * medium between too many and too few. There remains the question
 * of unique solutions, however. I fear there is no alternative but
 * to write - somehow! - a solver.
 * 
 * While I'm here, some notes on UI strategy for the parts of the
 * puzzle implementation that _aren't_ the generator:
 * 
 *  - data model is to track connections between adjacent squares,
 *    so that you aren't limited to extending a path out from each
 *    number but can also mark sections of path which you know
 *    _will_ come in handy later.
 * 
 *  - user interface is to click in one square and drag to an
 *    adjacent one, thus creating a link between them. We can
 *    probably tolerate rapid mouse motion causing a drag directly
 *    to a square which is a rook move away, but any other rapid
 *    motion is ambiguous and probably the best option is to wait
 *    until the mouse returns to a square we know how to reach.
 * 
 *  - a drag causing the current path to backtrack has the effect
 *    of removing bits of it.
 * 
 *  - the UI should enforce at all times the constraint that at
 *    most two links can come into any square.
 * 
 *  - my Cunning Plan for actually implementing this: the game_ui
 *    contains a grid-sized array, which is copied from the current
 *    game_state on starting a drag. While a drag is active, the
 *    contents of the game_ui is adjusted with every mouse motion,
 *    and is displayed _in place_ of the game_state itself. On
 *    termination of a drag, the game_ui array is copied back into
 *    the new game_state (or rather, a string move is encoded which
 *    has precisely the set of link changes to cause that effect).
 */

/*
 * Standard notation for directions.
 */
#define L 0
#define U 1
#define R 2
#define D 3
#define DX(dir) ( (dir)==L ? -1 : (dir)==R ? +1 : 0)
#define DY(dir) ( (dir)==U ? -1 : (dir)==D ? +1 : 0)

/*
 * Perform a breadth-first search over a grid of squares with the
 * colour of square (X,Y) given by grid[Y*w+X]. The search begins
 * at (x,y), and finds all squares which are the same colour as
 * (x,y) and reachable from it by orthogonal moves. On return:
 *  - dist[Y*w+X] gives the distance of (X,Y) from (x,y), or -1 if
 *    unreachable or a different colour
 *  - the returned value is the number of reachable squares,
 *    including (x,y) itself
 *  - list[0] up to list[returned value - 1] list those squares, in
 *    increasing order of distance from (x,y) (and in arbitrary
 *    order within that).
 */
static int bfs(int w, int h, int *grid, int x, int y, int *dist, int *list)
{
    int i, j, c, listsize, listdone;

    /*
     * Start by clearing the output arrays.
     */
    for (i = 0; i < w*h; i++)
	dist[i] = list[i] = -1;

    /*
     * Set up the initial list.
     */
    listsize = 1;
    listdone = 0;
    list[0] = y*w+x;
    dist[y*w+x] = 0;
    c = grid[y*w+x];

    /*
     * Repeatedly process a square and add any extra squares to the
     * end of list.
     */
    while (listdone < listsize) {
	i = list[listdone++];
	y = i / w;
	x = i % w;
	for (j = 0; j < 4; j++) {
	    int xx, yy, ii;

	    xx = x + DX(j);
	    yy = y + DY(j);
	    ii = yy*w+xx;

	    if (xx >= 0 && xx < w && yy >= 0 && yy < h &&
		grid[ii] == c && dist[ii] == -1) {
		dist[ii] = dist[i] + 1;
		assert(listsize < w*h);
		list[listsize++] = ii;
	    }
	}
    }

    return listsize;
}

struct genctx {
    int w, h;
    int *grid, *sparegrid, *sparegrid2, *sparegrid3;
    int *dist, *list;

    int npaths, pathsize;
    int *pathends, *sparepathends;     /* 2*npaths entries */
    int *pathspare;		       /* npaths entries */
    int *extends;		       /* 8*npaths entries */
};

static struct genctx *new_genctx(int w, int h)
{
    struct genctx *ctx = snew(struct genctx);
    ctx->w = w;
    ctx->h = h;
    ctx->grid = snewn(w * h, int);
    ctx->sparegrid = snewn(w * h, int);
    ctx->sparegrid2 = snewn(w * h, int);
    ctx->sparegrid3 = snewn(w * h, int);
    ctx->dist = snewn(w * h, int);
    ctx->list = snewn(w * h, int);
    ctx->npaths = ctx->pathsize = 0;
    ctx->pathends = ctx->sparepathends = ctx->pathspare = ctx->extends = NULL;
    return ctx;
}

static void free_genctx(struct genctx *ctx)
{
    sfree(ctx->grid);
    sfree(ctx->sparegrid);
    sfree(ctx->sparegrid2);
    sfree(ctx->sparegrid3);
    sfree(ctx->dist);
    sfree(ctx->list);
    sfree(ctx->pathends);
    sfree(ctx->sparepathends);
    sfree(ctx->pathspare);
    sfree(ctx->extends);
}

static int newpath(struct genctx *ctx)
{
    int n;

    n = ctx->npaths++;
    if (ctx->npaths > ctx->pathsize) {
	ctx->pathsize += 16;
	ctx->pathends = sresize(ctx->pathends, ctx->pathsize*2, int);
	ctx->sparepathends = sresize(ctx->sparepathends, ctx->pathsize*2, int);
	ctx->pathspare = sresize(ctx->pathspare, ctx->pathsize, int);
	ctx->extends = sresize(ctx->extends, ctx->pathsize*8, int);
    }
    return n;
}

static int is_endpoint(struct genctx *ctx, int x, int y)
{
    int w = ctx->w, h = ctx->h, c;

    assert(x >= 0 && x < w && y >= 0 && y < h);

    c = ctx->grid[y*w+x];
    if (c < 0)
	return FALSE;		       /* empty square is not an endpoint! */
    assert(c >= 0 && c < ctx->npaths);
    if (ctx->pathends[c*2] == y*w+x || ctx->pathends[c*2+1] == y*w+x)
	return TRUE;
    return FALSE;
}

/*
 * Tries to extend a path by one square in the given direction,
 * pushing other paths around if necessary. Returns TRUE on success
 * or FALSE on failure.
 */
static int extend_path(struct genctx *ctx, int path, int end, int direction)
{
    int w = ctx->w, h = ctx->h;
    int x, y, xe, ye, cut;
    int i, j, jp, n, first, last;

    assert(path >= 0 && path < ctx->npaths);
    assert(end == 0 || end == 1);

    /*
     * Find the endpoint of the path and the point we plan to
     * extend it into.
     */
    y = ctx->pathends[path * 2 + end] / w;
    x = ctx->pathends[path * 2 + end] % w;
    assert(x >= 0 && x < w && y >= 0 && y < h);

    xe = x + DX(direction);
    ye = y + DY(direction);
    if (xe < 0 || xe >= w || ye < 0 || ye >= h)
	return FALSE;		       /* could not extend in this direction */

    /*
     * We don't extend paths _directly_ into endpoints of other
     * paths, although we don't mind too much if a knock-on effect
     * of an extension is to push part of another path into a third
     * path's endpoint.
     */
    if (is_endpoint(ctx, xe, ye))
	return FALSE;

    /*
     * We can't extend a path back the way it came.
     */
    if (ctx->grid[ye*w+xe] == path)
	return FALSE;

    /*
     * Paths may not double back on themselves. Check if the new
     * point is adjacent to any point of this path other than (x,y).
     */
    for (j = 0; j < 4; j++) {
	int xf, yf;

	xf = xe + DX(j);
	yf = ye + DY(j);

	if (xf >= 0 && xf < w && yf >= 0 && yf < h &&
	    (xf != x || yf != y) && ctx->grid[yf*w+xf] == path)
	    return FALSE;
    }

    /*
     * Now we're convinced it's valid to _attempt_ the extension.
     * It may still fail if we run out of space to push other paths
     * into.
     *
     * So now we can set up our temporary data structures. We will
     * need:
     * 
     * 	- a spare copy of the grid on which to gradually move paths
     * 	  around (sparegrid)
     * 
     * 	- a second spare copy with which to remember how paths
     * 	  looked just before being cut (sparegrid2). FIXME: is
     * 	  sparegrid2 necessary? right now it's never different from
     * 	  grid itself
     * 
     * 	- a third spare copy with which to do the internal
     * 	  calculations involved in reconstituting a cut path
     * 	  (sparegrid3)
     * 
     * 	- something to track which paths currently need
     * 	  reconstituting after being cut, and which have already
     * 	  been cut (pathspare)
     * 
     * 	- a spare copy of pathends to store the altered states in
     * 	  (sparepathends)
     */
    memcpy(ctx->sparegrid, ctx->grid, w*h*sizeof(int));
    memcpy(ctx->sparegrid2, ctx->grid, w*h*sizeof(int));
    memcpy(ctx->sparepathends, ctx->pathends, ctx->npaths*2*sizeof(int));
    for (i = 0; i < ctx->npaths; i++)
	ctx->pathspare[i] = 0;	       /* 0=untouched, 1=broken, 2=fixed */

    /*
     * Working in sparegrid, actually extend the path. If it cuts
     * another, begin a loop in which we restore any cut path by
     * moving it out of the way.
     */
    cut = ctx->sparegrid[ye*w+xe];
    ctx->sparegrid[ye*w+xe] = path;
    ctx->sparepathends[path*2+end] = ye*w+xe;
    ctx->pathspare[path] = 2;	       /* this one is sacrosanct */
    if (cut >= 0) {
	assert(cut >= 0 && cut < ctx->npaths);
	ctx->pathspare[cut] = 1;       /* broken */

	while (1) {
	    for (i = 0; i < ctx->npaths; i++)
		if (ctx->pathspare[i] == 1)
		    break;
	    if (i == ctx->npaths)
		break;		       /* we're done */

	    /*
	     * Path i needs restoring. So walk along its original
	     * track (as given in sparegrid2) and see where it's
	     * been cut. Where it has, surround the cut points in
	     * the same colour, without overwriting already-fixed
	     * paths.
	     */
	    memcpy(ctx->sparegrid3, ctx->sparegrid, w*h*sizeof(int));
	    n = bfs(w, h, ctx->sparegrid2,
		    ctx->pathends[i*2] % w, ctx->pathends[i*2] / w,
		    ctx->dist, ctx->list);
	    first = last = -1;
if (ctx->sparegrid3[ctx->pathends[i*2]] != i ||
    ctx->sparegrid3[ctx->pathends[i*2+1]] != i) return FALSE;/* FIXME */
	    for (j = 0; j < n; j++) {
		jp = ctx->list[j];
		assert(ctx->dist[jp] == j);
		assert(ctx->sparegrid2[jp] == i);

		/*
		 * Wipe out the original path in sparegrid.
		 */
		if (ctx->sparegrid[jp] == i)
		    ctx->sparegrid[jp] = -1;

		/*
		 * Be prepared to shorten the path at either end if
		 * the endpoints have been stomped on.
		 */
		if (ctx->sparegrid3[jp] == i) {
		    if (first < 0)
			first = jp;
		    last = jp;
		}

		if (ctx->sparegrid3[jp] != i) {
		    int jx = jp % w, jy = jp / w;
		    int dx, dy;
		    for (dy = -1; dy <= +1; dy++)
			for (dx = -1; dx <= +1; dx++) {
			    int newp, newv;
			    if (!dy && !dx)
				continue;   /* central square */
			    if (jx+dx < 0 || jx+dx >= w ||
				jy+dy < 0 || jy+dy >= h)
				continue;   /* out of range */
			    newp = (jy+dy)*w+(jx+dx);
			    newv = ctx->sparegrid3[newp];
			    if (newv >= 0 && (newv == i ||
					      ctx->pathspare[newv] == 2))
				continue;   /* can't use this square */
			    ctx->sparegrid3[newp] = i;
			}
		}
	    }

	    if (first < 0 || last < 0)
		return FALSE;	       /* path is completely wiped out! */

	    /*
	     * Now we've covered sparegrid3 in possible squares for
	     * the new layout of path i. Find the actual layout
	     * we're going to use by bfs: we want the shortest path
	     * from one endpoint to the other.
	     */
	    n = bfs(w, h, ctx->sparegrid3, first % w, first / w,
		    ctx->dist, ctx->list);
	    if (ctx->dist[last] < 2) {
		/*
		 * Either there is no way to get between the path's
		 * endpoints, or the remaining endpoints simply
		 * aren't far enough apart to make the path viable
		 * any more. This means the entire push operation
		 * has failed.
		 */
		return FALSE;
	    }

	    /*
	     * Write the new path into sparegrid. Also save the new
	     * endpoint locations, in case they've changed.
	     */
	    jp = last;
	    j = ctx->dist[jp];
	    while (1) {
		int d;

		if (ctx->sparegrid[jp] >= 0) {
		    if (ctx->pathspare[ctx->sparegrid[jp]] == 2)
			return FALSE;  /* somehow we've hit a fixed path */
		    ctx->pathspare[ctx->sparegrid[jp]] = 1;   /* broken */
		}
		ctx->sparegrid[jp] = i;

		if (j == 0)
		    break;

		/*
		 * Now look at the neighbours of jp to find one
		 * which has dist[] one less.
		 */
		for (d = 0; d < 4; d++) {
		    int jx = (jp % w) + DX(d), jy = (jp / w) + DY(d);
		    if (jx >= 0 && jx < w && jy >= 0 && jy < w &&
			ctx->dist[jy*w+jx] == j-1) {
			jp = jy*w+jx;
			j--;
			break;
		    }
		}
		assert(d < 4);
	    }

	    ctx->sparepathends[i*2] = first;
	    ctx->sparepathends[i*2+1] = last;
//printf("new ends of path %d: %d,%d\n", i, first, last);
	    ctx->pathspare[i] = 2;     /* fixed */
	}
    }

    /*
     * If we got here, the extension was successful!
     */
    memcpy(ctx->grid, ctx->sparegrid, w*h*sizeof(int));
    memcpy(ctx->pathends, ctx->sparepathends, ctx->npaths*2*sizeof(int));
    return TRUE;
}

/*
 * Tries to add a new path to the grid.
 */
static int add_path(struct genctx *ctx, random_state *rs)
{
    int w = ctx->w, h = ctx->h;
    int i, ii, n;

    /*
     * Our strategy is:
     *  - randomly choose an empty square in the grid
     * 	- do a BFS from that point to find a long path starting
     * 	  from it
     *  - if we run out of viable empty squares, return failure.
     */

    /*
     * Use `sparegrid' to collect a list of empty squares.
     */
    n = 0;
    for (i = 0; i < w*h; i++)
	if (ctx->grid[i] == -1)
	    ctx->sparegrid[n++] = i;

    /*
     * Shuffle the grid.
     */
    for (i = n; i-- > 1 ;) {
	int k = random_upto(rs, i+1);
	if (k != i) {
	    int t = ctx->sparegrid[i];
	    ctx->sparegrid[i] = ctx->sparegrid[k];
	    ctx->sparegrid[k] = t;
	}
    }

    /*
     * Loop over it trying to add paths. This looks like a
     * horrifying N^4 algorithm (that is, (w*h)^2), but I predict
     * that in fact the worst case will very rarely arise because
     * when there's lots of grid space an attempt will succeed very
     * quickly.
     */
    for (ii = 0; ii < n; ii++) {
	int i = ctx->sparegrid[ii];
	int y = i / w, x = i % w, nsq;
	int r, c, j;

	/*
	 * BFS from here to find long paths.
	 */
	nsq = bfs(w, h, ctx->grid, x, y, ctx->dist, ctx->list);

	/*
	 * If there aren't any long enough, give up immediately.
	 */
	assert(nsq > 0);	       /* must be the start square at least! */
	if (ctx->dist[ctx->list[nsq-1]] < 3)
	    continue;

	/*
	 * Find the first viable endpoint in ctx->list (i.e. the
	 * first point with distance at least three). I could
	 * binary-search for this, but that would be O(log N)
	 * whereas in fact I can get a constant time bound by just
	 * searching up from the start - after all, there can be at
	 * most 13 points at _less_ than distance 3 from the
	 * starting one!
	 */
	for (j = 0; j < nsq; j++)
	    if (ctx->dist[ctx->list[j]] >= 3)
		break;
	assert(j < nsq);	       /* we tested above that there was one */

	/*
	 * Now we know that any element of `list' between j and nsq
	 * would be valid in principle. However, we want a few long
	 * paths rather than many small ones, so select only those
	 * elements which are either the maximum length or one
	 * below it.
	 */
	while (ctx->dist[ctx->list[j]] + 1 < ctx->dist[ctx->list[nsq-1]])
	    j++;
	r = j + random_upto(rs, nsq - j);
	j = ctx->list[r];

	/*
	 * And that's our endpoint. Mark the new path on the grid.
	 */
	c = newpath(ctx);
	ctx->pathends[c*2] = i;
	ctx->pathends[c*2+1] = j;
	ctx->grid[j] = c;
	while (j != i) {
	    int d, np, index, pts[4];
	    np = 0;
	    for (d = 0; d < 4; d++) {
		int xn = (j % w) + DX(d), yn = (j / w) + DY(d);
		if (xn >= 0 && xn < w && yn >= 0 && yn < w &&
		    ctx->dist[yn*w+xn] == ctx->dist[j] - 1)
		    pts[np++] = yn*w+xn;
	    }
	    if (np > 1)
		index = random_upto(rs, np);
	    else
		index = 0;
	    j = pts[index];
	    ctx->grid[j] = c;
	}

	return TRUE;
    }

    return FALSE;
}

/*
 * The main grid generation loop.
 */
static void gridgen_mainloop(struct genctx *ctx, random_state *rs)
{
    int w = ctx->w, h = ctx->h;
    int i, n;

    /*
     * The generation algorithm doesn't always converge. Loop round
     * until it does.
     */
    while (1) {
	for (i = 0; i < w*h; i++)
	    ctx->grid[i] = -1;
	ctx->npaths = 0;

	while (1) {
	    /*
	     * See if the grid is full.
	     */
	    for (i = 0; i < w*h; i++)
		if (ctx->grid[i] < 0)
		    break;
	    if (i == w*h)
		return;

#ifdef GENERATION_DIAGNOSTICS
	    {
		int x, y;
		for (y = 0; y < h; y++) {
		    printf("|");
		    for (x = 0; x < w; x++) {
			if (ctx->grid[y*w+x] >= 0)
			    printf("%2d", ctx->grid[y*w+x]);
			else
			    printf(" .");
		    }
		    printf(" |\n");
		}
	    }
#endif
	    /*
	     * Try adding a path.
	     */
	    if (add_path(ctx, rs)) {
#ifdef GENERATION_DIAGNOSTICS
		printf("added path\n");
#endif
		continue;
	    }

	    /*
	     * Try extending a path. First list all the possible
	     * extensions.
	     */
	    for (i = 0; i < ctx->npaths * 8; i++)
		ctx->extends[i] = i;
	    n = i;

	    /*
	     * Then shuffle the list.
	     */
	    for (i = n; i-- > 1 ;) {
		int k = random_upto(rs, i+1);
		if (k != i) {
		    int t = ctx->extends[i];
		    ctx->extends[i] = ctx->extends[k];
		    ctx->extends[k] = t;
		}
	    }

	    /*
	     * Now try each one in turn until one works.
	     */
	    for (i = 0; i < n; i++) {
		int p, d, e;
		p = ctx->extends[i];
		d = p % 4;
		p /= 4;
		e = p % 2;
		p /= 2;

#ifdef GENERATION_DIAGNOSTICS
		printf("trying to extend path %d end %d (%d,%d) in dir %d\n", p, e,
		       ctx->pathends[p*2+e] % w,
		       ctx->pathends[p*2+e] / w, d);
#endif
		if (extend_path(ctx, p, e, d)) {
#ifdef GENERATION_DIAGNOSTICS
		    printf("extended path %d end %d (%d,%d) in dir %d\n", p, e,
			   ctx->pathends[p*2+e] % w,
			   ctx->pathends[p*2+e] / w, d);
#endif
		    break;
		}
	    }

	    if (i < n)
		continue;

	    break;
	}
    }
}

/*
 * Wrapper function which deals with the boring bits such as
 * removing the solution from the generated grid, shuffling the
 * numeric labels and creating/disposing of the context structure.
 */
static int *gridgen(int w, int h, random_state *rs)
{
    struct genctx *ctx;
    int *ret;
    int i;

    ctx = new_genctx(w, h);

    gridgen_mainloop(ctx, rs);

    /*
     * There is likely to be an ordering bias in the numbers
     * (longer paths on lower numbers due to there having been more
     * grid space when laying them down). So we must shuffle the
     * numbers. We use ctx->pathspare for this.
     * 
     * This is also as good a time as any to shift to numbering
     * from 1, for display to the user.
     */
    for (i = 0; i < ctx->npaths; i++)
	ctx->pathspare[i] = i+1;
    for (i = ctx->npaths; i-- > 1 ;) {
	int k = random_upto(rs, i+1);
	if (k != i) {
	    int t = ctx->pathspare[i];
	    ctx->pathspare[i] = ctx->pathspare[k];
	    ctx->pathspare[k] = t;
	}
    }

    /* FIXME: remove this at some point! */
    {
	int y, x;
	for (y = 0; y < h; y++) {
	    printf("|");
	    for (x = 0; x < w; x++) {
		assert(ctx->grid[y*w+x] >= 0);
		printf("%2d", ctx->pathspare[ctx->grid[y*w+x]]);
	    }
	    printf(" |\n");
	}
	printf("\n");
    }

    /*
     * Clear the grid, and write in just the endpoints.
     */
    for (i = 0; i < w*h; i++)
	ctx->grid[i] = 0;
    for (i = 0; i < ctx->npaths; i++) {
	ctx->grid[ctx->pathends[i*2]] =
	    ctx->grid[ctx->pathends[i*2+1]] = ctx->pathspare[i];
    }

    ret = ctx->grid;
    ctx->grid = NULL;

    free_genctx(ctx);

    return ret;
}

#ifdef TEST_GEN

#define TEST_GENERAL

int main(void)
{
    int w = 10, h = 8;
    random_state *rs = random_init("12345", 5);
    int x, y, i, *grid;

    for (i = 0; i < 10; i++) {
	grid = gridgen(w, h, rs);

	for (y = 0; y < h; y++) {
	    printf("|");
	    for (x = 0; x < w; x++) {
		if (grid[y*w+x] > 0)
		    printf("%2d", grid[y*w+x]);
		else
		    printf(" .");
	    }
	    printf(" |\n");
	}
	printf("\n");

	sfree(grid);
    }

    return 0;
}
#endif

#ifdef TEST_GENERAL
#include <stdarg.h>

void fatal(const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "fatal error: ");

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fprintf(stderr, "\n");
    exit(1);
}
#endif
