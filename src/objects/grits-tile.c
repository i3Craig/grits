/*
 * Copyright (C) 2009-2010, 2012 Andy Spencer <andy753421@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * SECTION:grits-tile
 * @short_description: Latitude/longitude overlays
 *
 * Each #GritsTile corresponds to a latitude/longitude box on the surface of
 * the earth. When drawn, the #GritsTile renders an images associated with it
 * to the surface of the earth. This is primarily used to draw ground overlays.
 *
 * Each GritsTile can be split into subtiles in order to draw higher resolution
 * overlays. Pointers to subtitles are stored in the parent tile and a parent
 * pointer is stored in each child.
 *
 * Each #GritsTile has a data filed which must be set by the user in order for
 * the tile to be drawn. When used with GritsOpenGL the data must be an integer
 * representing the OpenGL texture to use when drawing the tile.
 */

#define GL_GLEXT_PROTOTYPES
#include <config.h>
#include <math.h>
#include <string.h>
#include "gtkgl.h"
#include "grits-tile.h"

static guint  grits_tile_mask = 0;

gchar *grits_tile_path_table[2][2] = {
	{"00.", "01."},
	{"10.", "11."},
};

/**
 * grits_tile_new:
 * @parent: the parent for the tile, or NULL
 * @n:      the northern border of the tile
 * @s:      the southern border of the tile
 * @e:      the eastern border of the tile
 * @w:      the western border of the tile
 *
 * Create a tile associated with a particular latitude/longitude box.
 *
 * Returns: the new #GritsTile
 */
GritsTile *grits_tile_new(GritsTile *parent,
	gdouble n, gdouble s, gdouble e, gdouble w)
{
	GritsTile *tile = g_object_new(GRITS_TYPE_TILE, NULL);
	tile->parent = parent;
	tile->atime  = time(NULL);
	grits_bounds_set_bounds(&tile->coords, 0, 1, 1, 0);
	grits_bounds_set_bounds(&tile->edge, n, s, e, w);
	if (parent)
		tile->proj   = parent->proj;
	return tile;
}

/**
 * grits_tile_get_path:
 * @child: the tile to generate a path for
 *
 * Generate a string representation of a tiles location in a group of nested
 * tiles. The string returned consists of groups of two digits separated by a
 * delimiter. Each group of digits the tiles location with respect to it's
 * parent tile.
 *
 * Returns: the path representing the tiles's location
 */
gchar *grits_tile_get_path(GritsTile *child)
{
	/* This could be easily cached if necessary */
	int x, y;
	GList *parts = NULL;
	for (GritsTile *parent = child->parent; parent; child = parent, parent = child->parent)
		grits_tile_foreach_index(child, x, y)
			if (parent->children[x][y] == child)
				parts = g_list_prepend(parts, grits_tile_path_table[x][y]);
	GString *path = g_string_new("");
	for (GList *cur = parts; cur; cur = cur->next)
		g_string_append(path, cur->data);
	g_list_free(parts);
	return g_string_free(path, FALSE);
}

static gdouble _grits_tile_get_min_dist(GritsPoint *eye, GritsBounds *bounds)
{
	GritsPoint pos = {};
	pos.lat = eye->lat > bounds->n ? bounds->n :
	          eye->lat < bounds->s ? bounds->s : eye->lat;
	pos.lon = eye->lon > bounds->e ? bounds->e :
	          eye->lon < bounds->w ? bounds->w : eye->lon;
	//if (eye->lat == pos.lat && eye->lon == pos.lon)
	//	return elev; /* Shortcut? */
	gdouble a[3], b[3];
	lle2xyz(eye->lat, eye->lon, eye->elev, a+0, a+1, a+2);
	lle2xyz(pos.lat,  pos.lon,  pos.elev,  b+0, b+1, b+2);
	return distd(a, b);
}

static gboolean _grits_tile_precise(GritsPoint *eye, GritsBounds *bounds,
		gdouble max_res, gint width, gint height)
{
	gdouble min_dist  = _grits_tile_get_min_dist(eye, bounds);
	gdouble view_res  = MPPX(min_dist);

	gdouble lat_point = bounds->n < 0 ? bounds->n :
	                    bounds->s > 0 ? bounds->s : 0;
	gdouble lon_dist  = bounds->e - bounds->w;
	gdouble tile_res  = ll2m(lon_dist, lat_point)/width;

	/* This isn't really right, but it helps with memory since we don't
	 * (yet?) test if the tile would be drawn */
	gdouble scale = eye->elev / min_dist;
	view_res /= scale;
	view_res *= 1.8;
	//view_res /= 1.4; /* make it a little nicer, not sure why this is needed */
	//g_message("tile=(%7.2f %7.2f %7.2f %7.2f) "
	//          "eye=(%9.1f %9.1f %9.1f) "
	//          "elev=%9.1f / dist=%9.1f = %f",
	//		bounds->n, bounds->s, bounds->e, bounds->w,
	//		eye->lat, eye->lon, eye->elev,
	//		eye->elev, min_dist, scale);

	return tile_res < max_res ||
	       tile_res < view_res;
}

static void _grits_tile_split_latlon(GritsTile *tile)
{
	//g_debug("GritsTile: split - %p", tile);
	const gdouble rows = G_N_ELEMENTS(tile->children);
	const gdouble cols = G_N_ELEMENTS(tile->children[0]);
	const gdouble lat_dist = tile->edge.n - tile->edge.s;
	const gdouble lon_dist = tile->edge.e - tile->edge.w;
	const gdouble lat_step = lat_dist / rows;
	const gdouble lon_step = lon_dist / cols;

	int row, col;
	grits_tile_foreach_index(tile, row, col) {
		if (!tile->children[row][col])
			tile->children[row][col] =
				grits_tile_new(tile, 0, 0, 0, 0);
		/* Set edges aferwards so that north and south
		 * get reset for mercator projections */
		GritsTile *child = tile->children[row][col];
		child->edge.n = tile->edge.n - lat_step*(row+0);
		child->edge.s = tile->edge.n - lat_step*(row+1);
		child->edge.e = tile->edge.w + lon_step*(col+1);
		child->edge.w = tile->edge.w + lon_step*(col+0);
	}
}

static void _grits_tile_split_mercator(GritsTile *tile)
{
	GritsTile *child = NULL;
	GritsBounds tmp = tile->edge;

	/* Project */
	tile->edge.n = asinh(tan(deg2rad(tile->edge.n)));
	tile->edge.s = asinh(tan(deg2rad(tile->edge.s)));

	_grits_tile_split_latlon(tile);

	/* Convert back to lat-lon */
	tile->edge = tmp;
	grits_tile_foreach(tile, child) {
		child->edge.n = rad2deg(atan(sinh(child->edge.n)));
		child->edge.s = rad2deg(atan(sinh(child->edge.s)));
	}
}

/**
 * grits_tile_update:
 * @root:      the root tile to split
 * @eye:       the point the tile is viewed from, for calculating distances
 * @res:       a maximum resolution in meters per pixel to split tiles to
 * @width:     width in pixels of the image associated with the tile
 * @height:    height in pixels of the image associated with the tile
 * @load_func: function used to load the image when a new tile is created
 * @user_data: user data to past to the load function
 *
 * Recursively split a tile into children of appropriate detail. The resolution
 * of the tile in pixels per meter is compared to the resolution which the tile
 * is being drawn at on the screen. If the screen resolution is insufficient
 * the tile is recursively subdivided until a sufficient resolution is
 * achieved.
 */
void grits_tile_update(GritsTile *tile, GritsPoint *eye,
		gdouble res, gint width, gint height,
		GritsTileLoadFunc load_func, gpointer user_data)
{
	GritsTile *child;

	if (tile == NULL)
		return;

	//g_debug("GritsTile: update - %p->atime = %u",
	//		tile, (guint)tile->atime);

	/* Is the parent tile's texture high enough
	 * resolution for this part? */
	gint xs = G_N_ELEMENTS(tile->children);
	gint ys = G_N_ELEMENTS(tile->children[0]);
	if (tile->parent && _grits_tile_precise(eye, &tile->edge,
				res, width/xs, height/ys)) {
		GRITS_OBJECT(tile)->hidden = TRUE;
		return;
	}

	/* Load the tile */
	if (!tile->load && !tile->data && !tile->tex && !tile->pixels && !tile->pixbuf)
		load_func(tile, user_data);
	tile->atime = time(NULL);
	tile->load  = TRUE;
	GRITS_OBJECT(tile)->hidden = FALSE;

	/* Split tile if needed */
	grits_tile_foreach(tile, child) {
		if (child == NULL) {
			switch (tile->proj) {
			case GRITS_PROJ_LATLON:   _grits_tile_split_latlon(tile);   break;
			case GRITS_PROJ_MERCATOR: _grits_tile_split_mercator(tile); break;
			}
		}
	}

	/* Update recursively */
	grits_tile_foreach(tile, child)
		grits_tile_update(child, eye, res, width, height,
				load_func, user_data);
}

static void _grits_tile_queue_draw(GritsTile *tile)
{
	while (!GRITS_OBJECT(tile)->viewer && tile->parent)
		tile = tile->parent;
	grits_object_queue_draw(GRITS_OBJECT(tile));
}

/**
 * grits_tile_load_pixels:
 * @tile:   the tile to load data into
 * @pixels: buffered pixel data
 * @width:  width of the pixel buffer (in pixels)
 * @height: height of the pixel buffer (in pixels)
 * @alpha:  TRUE if the pixel data contains an alpha channel
 *
 * Load tile data from an in memory pixel buffer.
 *
 * This function is thread safe and my be called from outside the main thread.
 *
 * Ownership of the pixel buffer is passed to the tile, it should not be freed
 * or modified after calling this function.
 *
 * Returns: TRUE if the image was loaded successfully
 */
gboolean grits_tile_load_pixels(GritsTile *tile, guchar *pixels,
		gint width, gint height, gint alpha)
{
	g_debug("GritsTile: load_pixels - %p -> %p (%dx%d:%d)",
			tile, pixels, width, height, alpha);

	/* Copy pixbuf data for callback */
	tile->width  = width;
	tile->height = height;
	tile->alpha  = alpha;
	tile->pixels = pixels;

	/* Queue OpenGL texture load/draw */
	_grits_tile_queue_draw(tile);

	return TRUE;
}

/**
 * grits_tile_load_file:
 * @tile: the tile to load data into
 * @file: path to an image file to load
 *
 * Load tile data from a GdkPixbuf
 * This function is thread safe and my be called from outside the main thread.
 *
 * Returns: TRUE if the image was loaded successfully
 */
gboolean grits_tile_load_pixbuf(GritsTile *tile, GdkPixbuf *pixbuf)
{
	g_debug("GritsTile: load_pixbuf %p -> %p", tile, pixbuf);

	/* Copy pixbuf data for callback */
	tile->pixbuf = g_object_ref(pixbuf);
	tile->width  = gdk_pixbuf_get_width(pixbuf);
	tile->height = gdk_pixbuf_get_height(pixbuf);
	tile->alpha  = gdk_pixbuf_get_has_alpha(pixbuf);

	/* Queue OpenGL texture load/draw */
	_grits_tile_queue_draw(tile);

	return TRUE;
}

/**
 * grits_tile_load_file:
 * @tile: the tile to load data into
 * @file: path to an image file to load
 *
 * Load tile data from an image file
 * This function is thread safe and my be called from outside the main thread.
 *
 * Returns: TRUE if the image was loaded successfully
 */
gboolean grits_tile_load_file(GritsTile *tile, const gchar *file)
{
	g_debug("GritsTile: load_file %p -> %s", tile, file);

	/* Copy pixbuf data for callback */
	tile->pixbuf = gdk_pixbuf_new_from_file(file, NULL);
	if (!tile->pixbuf)
		return FALSE;
	tile->width  = gdk_pixbuf_get_width(tile->pixbuf);
	tile->height = gdk_pixbuf_get_height(tile->pixbuf);
	tile->alpha  = gdk_pixbuf_get_has_alpha(tile->pixbuf);

	/* Queue OpenGL texture load/draw */
	_grits_tile_queue_draw(tile);

	return TRUE;
}

/**
 * grits_tile_find:
 * @root: the root tile to search from
 * @lat:  target latitude
 * @lon:  target longitude
 *
 * Locate the subtile with the highest resolution which contains the given
 * lat/lon point.
 *
 * Returns: the child tile
 */
GritsTile *grits_tile_find(GritsTile *root, gdouble lat, gdouble lon)
{
	gint    rows = G_N_ELEMENTS(root->children);
	gint    cols = G_N_ELEMENTS(root->children[0]);

	gdouble lat_step = (root->edge.n - root->edge.s) / rows;
	gdouble lon_step = (root->edge.e - root->edge.w) / cols;

	gdouble lat_offset = root->edge.n - lat;;
	gdouble lon_offset = lon - root->edge.w;

	gint    row = lat_offset / lat_step;
	gint    col = lon_offset / lon_step;

	if (lon == 180) col--;
	if (lat == -90) row--;

	//if (lon == 180 || lon == -180)
	//	g_message("lat=%f,lon=%f step=%f,%f off=%f,%f row=%d/%d,col=%d/%d",
	//		lat,lon, lat_step,lon_step, lat_offset,lon_offset, row,rows,col,cols);

	if (row < 0 || row >= rows || col < 0 || col >= cols)
		return NULL;
	else if (root->children[row][col] && root->children[row][col]->data)
		return grits_tile_find(root->children[row][col], lat, lon);
	else
		return root;
}

/**
 * grits_tile_gc:
 * @root:      the root tile to start garbage collection at
 * @atime:     most recent time at which tiles will be kept
 * @free_func: function used to free the image when a new tile is collected
 * @user_data: user data to past to the free function
 *
 * Garbage collect old tiles. This removes and deallocate tiles that have not
 * been used since before @atime.
 *
 * Returns: a pointer to the original tile, or NULL if it was garbage collected
 */
GritsTile *grits_tile_gc(GritsTile *root, time_t atime,
		GritsTileFreeFunc free_func, gpointer user_data)
{
	if (!root)
		return NULL;
	gboolean has_children = FALSE;
	int x, y;
	grits_tile_foreach_index(root, x, y) {
		root->children[x][y] = grits_tile_gc(
				root->children[x][y], atime,
				free_func, user_data);
		if (root->children[x][y])
			has_children = TRUE;
	}
	//g_debug("GritsTile: gc - %p kids=%d time=%d data=%d load=%d",
	//	root, !!has_children, root->atime < atime, !!root->data, !!root->load);
	int thread_safe = !root->load || root->data || root->tex || root->pixels || root->pixbuf;
	if (root->parent && !has_children && root->atime < atime && thread_safe) {
		//g_debug("GritsTile: gc/free - %p", root);
		if (root->pixbuf)
			g_object_unref(root->pixbuf);
		if (root->pixels)
			g_free(root->pixels);
		if (root->tex)
			glDeleteTextures(1, &root->tex);
		if (root->data) {
			if (free_func)
				free_func(root, user_data);
			else
				g_free(root->data);
		}
		g_object_unref(root);
		return NULL;
	}
	return root;
}

/* Use GObject for this */
/**
 * grits_tile_free:
 * @root:      the root tile to free
 * @free_func: function used to free the image when a new tile is collected
 * @user_data: user data to past to the free function
 *
 * Recursively free a tile and all it's children.
 */
void grits_tile_free(GritsTile *root, GritsTileFreeFunc free_func, gpointer user_data)
{
	if (!root)
		return;
	GritsTile *child;
	grits_tile_foreach(root, child)
		grits_tile_free(child, free_func, user_data);
	if (free_func)
		free_func(root, user_data);
	g_object_unref(root);
}

/* Load texture mask so we can draw a texture to just a part of a triangle */
static guint _grits_tile_load_mask(void)
{
	guint tex;
	const int width = 256, height = 256;
	guint8 *bytes = g_malloc(width*height);
	memset(bytes, 0xff, width*height);
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, width, height, 0,
			GL_ALPHA, GL_UNSIGNED_BYTE, bytes);

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	g_free(bytes);
	return tex;
}

/* Load the texture from saved pixel data */
static gboolean _grits_tile_load_tex(GritsTile *tile)
{
	/* Abort for null tiles */
	if (!tile)
		return FALSE;

	/* Defer loading of hidden tiles */
	if (GRITS_OBJECT(tile)->hidden)
		return FALSE;

	/* If we're already done loading the text stop */
	if (tile->tex)
		return TRUE;

	/* Check if the tile has data yet */
	if (!tile->pixels && !tile->pixbuf)
		return FALSE;

	/* Get correct pixel buffer */
	guchar *pixels = tile->pixels ?:
		gdk_pixbuf_get_pixels(tile->pixbuf);

	/* Create texture */
	g_debug("GritsTile: load_tex");
	glGenTextures(1, &tile->tex);
	glBindTexture(GL_TEXTURE_2D, tile->tex);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, 4, tile->width, tile->height, 0,
			(tile->alpha ? GL_RGBA : GL_RGB), GL_UNSIGNED_BYTE, pixels);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	/* Free data */
	if (tile->pixbuf) {
		g_object_unref(tile->pixbuf);
		tile->pixbuf = NULL;
	}
	if (tile->pixels) {
		g_free(tile->pixels);
		tile->pixels = NULL;
	}

	return TRUE;

}

/* Draw a single tile */
static void grits_tile_draw_one(GritsTile *tile, GritsOpenGL *opengl, GList *triangles)
{
	if (!tile || !tile->tex)
		return;
	if (!triangles)
		g_warning("GritsOpenGL: _draw_tiles - No triangles to draw: edges=%f,%f,%f,%f",
			tile->edge.n, tile->edge.s, tile->edge.e, tile->edge.w);

	//g_message("drawing %4d triangles for tile edges=%7.2f,%7.2f,%7.2f,%7.2f",
	//		g_list_length(triangles), tile->edge.n, tile->edge.s, tile->edge.e, tile->edge.w);
	tile->atime = time(NULL);

	gdouble n = tile->edge.n;
	gdouble s = tile->edge.s;
	gdouble e = tile->edge.e;
	gdouble w = tile->edge.w;

	gdouble londist = e - w;
	gdouble latdist = n - s;

	gdouble xscale = tile->coords.e - tile->coords.w;
	gdouble yscale = tile->coords.s - tile->coords.n;

	glPolygonOffset(0, -tile->zindex);

	for (GList *cur = triangles; cur; cur = cur->next) {
		RoamTriangle *tri = cur->data;

		gdouble lat[3] = {tri->p.r->lat, tri->p.m->lat, tri->p.l->lat};
		gdouble lon[3] = {tri->p.r->lon, tri->p.m->lon, tri->p.l->lon};

		if (lon[0] < -90 || lon[1] < -90 || lon[2] < -90) {
			if (lon[0] > 90) lon[0] -= 360;
			if (lon[1] > 90) lon[1] -= 360;
			if (lon[2] > 90) lon[2] -= 360;
		}

		gdouble xy[3][2] = {
			{(lon[0]-w)/londist, 1-(lat[0]-s)/latdist},
			{(lon[1]-w)/londist, 1-(lat[1]-s)/latdist},
			{(lon[2]-w)/londist, 1-(lat[2]-s)/latdist},
		};

		//if ((lat[0] == 90 && (xy[0][0] < 0 || xy[0][0] > 1)) ||
		//    (lat[1] == 90 && (xy[1][0] < 0 || xy[1][0] > 1)) ||
		//    (lat[2] == 90 && (xy[2][0] < 0 || xy[2][0] > 1)))
		//	g_message("w,e=%4.f,%4.f   "
		//	          "lat,lon,x,y="
		//	          "%4.1f,%4.0f,%4.2f,%4.2f   "
		//	          "%4.1f,%4.0f,%4.2f,%4.2f   "
		//	          "%4.1f,%4.0f,%4.2f,%4.2f   ",
		//		w,e,
		//		lat[0], lon[0], xy[0][0], xy[0][1],
		//		lat[1], lon[1], xy[1][0], xy[1][1],
		//		lat[2], lon[2], xy[2][0], xy[2][1]);

		/* Fix poles */
		if (lat[0] == 90 || lat[0] == -90) xy[0][0] = 0.5;
		if (lat[1] == 90 || lat[1] == -90) xy[1][0] = 0.5;
		if (lat[2] == 90 || lat[2] == -90) xy[2][0] = 0.5;

		/* Scale to tile coords */
		for (int i = 0; i < 3; i++) {
			xy[i][0] = tile->coords.w + xy[i][0]*xscale;
			xy[i][1] = tile->coords.n + xy[i][1]*yscale;
		}

		/* Draw triangle */
		glBindTexture(GL_TEXTURE_2D, tile->tex);
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glBegin(GL_TRIANGLES);
		glNormal3dv(tri->p.r->norm); glMultiTexCoord2dv(GL_TEXTURE0, xy[0]); glMultiTexCoord2dv(GL_TEXTURE1, xy[0]); glVertex3dv((double*)tri->p.r);
		glNormal3dv(tri->p.m->norm); glMultiTexCoord2dv(GL_TEXTURE0, xy[1]); glMultiTexCoord2dv(GL_TEXTURE1, xy[1]); glVertex3dv((double*)tri->p.m);
		glNormal3dv(tri->p.l->norm); glMultiTexCoord2dv(GL_TEXTURE0, xy[2]); glMultiTexCoord2dv(GL_TEXTURE1, xy[2]); glVertex3dv((double*)tri->p.l);
		glEnd();
	}
}

/* Draw the tile */
static gboolean grits_tile_draw_rec(GritsTile *tile, GritsOpenGL *opengl)
{
	//g_debug("GritsTile: draw_rec - tile=%p, data=%d, load=%d, hide=%d", tile,
	//		tile ? !!tile->data : 0,
	//		tile ? !!tile->load : 0,
	//		tile ? !!GRITS_OBJECT(tile)->hidden : 0);

	if (!_grits_tile_load_tex(tile))
		return FALSE;

	GritsTile *child = NULL;

	/* Draw child tiles */
	gboolean draw_parent = FALSE;
	grits_tile_foreach(tile, child)
		if (!grits_tile_draw_rec(child, opengl))
			draw_parent = TRUE;

	/* Draw parent tile underneath using depth test */
	if (draw_parent) {
		GList *triangles = roam_sphere_get_intersect(opengl->sphere, FALSE,
				tile->edge.n, tile->edge.s, tile->edge.e, tile->edge.w);
		grits_tile_draw_one(tile, opengl, triangles);
		g_list_free(triangles);
	}

	return TRUE;
}

static void grits_tile_draw(GritsObject *tile, GritsOpenGL *opengl)
{
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.1);
	glEnable(GL_POLYGON_OFFSET_FILL);
	glEnable(GL_BLEND);

	/* Setup texture mask */
	if (!grits_tile_mask)
		grits_tile_mask = _grits_tile_load_mask();
	glActiveTexture(GL_TEXTURE1);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, grits_tile_mask);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	/* Setup texture */
	glActiveTexture(GL_TEXTURE0);
	glEnable(GL_TEXTURE_2D);

	/* Hack to show maps tiles with better color */
	if (GRITS_TILE(tile)->proj == GRITS_PROJ_MERCATOR) {
		float material_emission[] = {0.5, 0.5, 0.5, 1.0};
		glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, material_emission);
	}

	/* Draw all tiles */
	grits_tile_draw_rec(GRITS_TILE(tile), opengl);

	/* Disable texture mask */
	glActiveTexture(GL_TEXTURE1);
	glDisable(GL_TEXTURE_2D);
	glActiveTexture(GL_TEXTURE0);
}


/* GObject code */
G_DEFINE_TYPE(GritsTile, grits_tile, GRITS_TYPE_OBJECT);
static void grits_tile_init(GritsTile *tile)
{
}

static void grits_tile_class_init(GritsTileClass *klass)
{
	g_debug("GritsTile: class_init");
	GritsObjectClass *object_class = GRITS_OBJECT_CLASS(klass);
	object_class->draw = grits_tile_draw;
}
