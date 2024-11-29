/*
 * Copyright (C) 2009-2011 Andy Spencer <andy753421@gmail.com>
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
 * SECTION:elev
 * @short_description: Elevation plugin
 *
 * #GritsPluginElev provides access to ground elevation. It does this in two ways:
 * First, it provides a height function used by the viewer when drawing the
 * world. Second, it can load the elevation data into an image and draw a
 * greyscale elevation overlay on the planets surface.
 */

#include <time.h>
#include <glib/gstdio.h>

#include <grits.h>

#include "elev.h"

/* Configuration */
#define LOAD_BIL       TRUE
#define LOAD_TEX       FALSE

/* Tile size constnats */
#define MAX_RESOLUTION 50
#define TILE_WIDTH     1024
#define TILE_HEIGHT    512
#define TILE_CHANNELS  4
#define TILE_SIZE      (TILE_WIDTH*TILE_HEIGHT*sizeof(guint16))

static gdouble _height_func(gdouble lat, gdouble lon, gpointer _elev)
{
	GritsPluginElev *elev = _elev;
	if (!elev) return 0;

	GritsTile *tile = grits_tile_find(elev->tiles, lat, lon);
	if (!tile) return 0;

	guint16 *bil = tile->data;
	if (!bil)  return 0;

	gint w = TILE_WIDTH;
	gint h = TILE_HEIGHT;

	gdouble ymin  = tile->edge.s;
	gdouble ymax  = tile->edge.n;
	gdouble xmin  = tile->edge.w;
	gdouble xmax  = tile->edge.e;

	gdouble xdist = xmax - xmin;
	gdouble ydist = ymax - ymin;

	gdouble x =    (lon-xmin)/xdist  * w;
	gdouble y = (1-(lat-ymin)/ydist) * h;

	gdouble x_rem = x - (int)x;
	gdouble y_rem = y - (int)y;
	guint x_flr = (int)x;
	guint y_flr = (int)y;

	//if (lon == 180 || lon == -180)
	//	g_message("lon=%f w=%d min=%f max=%f dist=%f x=%f rem=%f flr=%d",
	//	           lon,   w,  xmin,  xmax,  xdist,   x, x_rem, x_flr);

	/* TODO: Fix interpolation at edges:
	 *   - Pad these at the edges instead of wrapping/truncating
	 *   - Figure out which pixels to index (is 0,0 edge, center, etc) */
	gint16 px00 = bil[MIN((y_flr  ),h-1)*w + MIN((x_flr  ),w-1)];
	gint16 px10 = bil[MIN((y_flr  ),h-1)*w + MIN((x_flr+1),w-1)];
	gint16 px01 = bil[MIN((y_flr+1),h-1)*w + MIN((x_flr  ),w-1)];
	gint16 px11 = bil[MIN((y_flr+1),h-1)*w + MIN((x_flr+1),w-1)];

	return px00 * (1-x_rem) * (1-y_rem) +
	       px10 * (  x_rem) * (1-y_rem) +
	       px01 * (1-x_rem) * (  y_rem) +
	       px11 * (  x_rem) * (  y_rem);
}

/**********************
 * Loader and Freeers *
 **********************/

static guint16 *_load_bil(gchar *path)
{
	gsize len;
	gchar *data = NULL;
	g_file_get_contents(path, &data, &len, NULL);
	g_debug("GritsPluginElev: load_bil %p", data);
	if (len != TILE_SIZE) {
		g_warning("GritsPluginElev: _load_bil - unexpected tile size %ld, != %ld",
				(glong)len, (glong)TILE_SIZE);
		g_free(data);
		return NULL;
	}
	return (guint16*)data;
}

static guchar *_load_pixels(guint16 *bil)
{
	g_assert(TILE_CHANNELS == 4);

	guchar (*pixels)[TILE_WIDTH][TILE_CHANNELS]
		= g_malloc0(TILE_HEIGHT * TILE_WIDTH * TILE_CHANNELS);

	for (int r = 0; r < TILE_HEIGHT; r++) {
		for (int c = 0; c < TILE_WIDTH; c++) {
			gint16 value = bil[r*TILE_WIDTH + c];
			guchar color = (float)value/8848 * 255;
			//guchar color = (float)(MAX(value,0))/8848 * 255;
			pixels[r][c][0] = color;
			pixels[r][c][1] = color;
			pixels[r][c][2] = color;
			pixels[r][c][3] = 0xff;
		}
	}

	g_debug("GritsPluginElev: load_pixels %p", pixels);
	return (guchar*)pixels;
}

static void _load_tile_thread(gpointer _tile, gpointer _elev)
{
	GritsTile       *tile = _tile;
	GritsPluginElev *elev = _elev;

	g_debug("GritsPluginElev: _load_tile_thread start %p - tile=%p",
			g_thread_self(), tile);
	if (elev->aborted) {
		g_debug("GritsPluginElev: _load_tile_thread - aborted");
		return;
	}

	/* Download tile */
	gchar *path = grits_wms_fetch(elev->wms, tile, GRITS_ONCE, NULL, NULL);
	if (!path)
		return;

	/* Load bil */
	guint16 *bil = _load_bil(path);
	g_free(path);
	if (!bil)
		return;

	/* Set hight function (TODO: from main thread?) */
	if (LOAD_BIL) {
		tile->data = bil;
		grits_viewer_set_height_func(elev->viewer, &tile->edge,
				_height_func, elev, TRUE);
	}

	/* Load pixels for grayscale height textures */
	if (LOAD_TEX) {
		guchar *pixels = _load_pixels(bil);
		grits_tile_load_pixels(tile, pixels,
			TILE_WIDTH, TILE_HEIGHT, TILE_CHANNELS==4);
	}

	/* Free bill if we're not interested in a hight function */
	if (!LOAD_BIL)
		g_free(bil);

	/* Load the GL texture from the main thread */
	g_debug("GritsPluginElev: _load_tile_thread end %p", g_thread_self());
}

static void _load_tile_func(GritsTile *tile, gpointer _elev)
{
	g_debug("GritsPluginElev: _load_tile_func - tile=%p", tile);
	GritsPluginElev *elev = _elev;
	g_thread_pool_push(elev->threads, tile, NULL);
}

/*************
 * Callbacks *
 *************/
static void _on_location_changed(GritsViewer *viewer,
		gdouble lat, gdouble lon, gdouble elevation, GritsPluginElev *elev)
{
	GritsPoint eye = {lat, lon, elevation};
	grits_tile_update(elev->tiles, &eye,
			MAX_RESOLUTION, TILE_WIDTH, TILE_WIDTH,
			_load_tile_func, elev);
	grits_tile_gc(elev->tiles, time(NULL)-10, NULL, elev);
}

/***********
 * Methods *
 ***********/
/**
 * grits_plugin_elev_new:
 * @viewer: the #GritsViewer to use for drawing
 *
 * Create a new instance of the elevation plugin.
 *
 * Returns: the new #GritsPluginElev
 */
GritsPluginElev *grits_plugin_elev_new(GritsViewer *viewer)
{
	g_debug("GritsPluginElev: new");
	GritsPluginElev *elev = g_object_new(GRITS_TYPE_PLUGIN_ELEV, NULL);
	elev->viewer = g_object_ref(viewer);

	/* Load initial tiles */
	gdouble lat, lon, elevation;
	grits_viewer_get_location(viewer, &lat, &lon, &elevation);
	_on_location_changed(viewer, lat, lon, elevation, elev);

	/* Connect signals */
	elev->sigid = g_signal_connect(elev->viewer, "location-changed",
			G_CALLBACK(_on_location_changed), elev);

	/* Add renderers */
	if (LOAD_TEX)
		grits_viewer_add(viewer, GRITS_OBJECT(elev->tiles), GRITS_LEVEL_WORLD, FALSE);

	return elev;
}


/****************
 * GObject code *
 ****************/
/* Plugin init */
static void grits_plugin_elev_plugin_init(GritsPluginInterface *iface);
G_DEFINE_TYPE_WITH_CODE(GritsPluginElev, grits_plugin_elev, G_TYPE_OBJECT,
		G_IMPLEMENT_INTERFACE(GRITS_TYPE_PLUGIN,
			grits_plugin_elev_plugin_init));
static void grits_plugin_elev_plugin_init(GritsPluginInterface *iface)
{
	g_debug("GritsPluginElev: plugin_init");
	/* Add methods to the interface */
}
/* Class/Object init */
static void grits_plugin_elev_init(GritsPluginElev *elev)
{
	g_debug("GritsPluginElev: init");
	/* Set defaults */
	elev->threads = g_thread_pool_new(_load_tile_thread, elev, 1, FALSE, NULL);
	elev->tiles = grits_tile_new(NULL, NORTH, SOUTH, EAST, WEST);
	elev->wms   = grits_wms_new(
		"http://www.nasa.network.com/elev", "mergedSrtm", "application/bil",
		"srtm/", "bil", TILE_WIDTH, TILE_HEIGHT);
	g_object_ref(elev->tiles);
}
static void grits_plugin_elev_dispose(GObject *gobject)
{
	g_debug("GritsPluginElev: dispose");
	GritsPluginElev *elev = GRITS_PLUGIN_ELEV(gobject);
	elev->aborted = TRUE;
	/* Drop references */
	if (elev->viewer) {
		GritsViewer *viewer = elev->viewer;
		g_signal_handler_disconnect(viewer, elev->sigid);
		grits_http_abort(elev->wms->http);
		g_thread_pool_free(elev->threads, TRUE, TRUE);
		elev->viewer = NULL;
		if (LOAD_BIL)
			grits_viewer_clear_height_func(viewer);
		if (LOAD_TEX)
			grits_object_destroy_pointer(&elev->tiles);
		g_object_unref(viewer);
	}
	G_OBJECT_CLASS(grits_plugin_elev_parent_class)->dispose(gobject);
}
static void grits_plugin_elev_finalize(GObject *gobject)
{
	g_debug("GritsPluginElev: finalize");
	GritsPluginElev *elev = GRITS_PLUGIN_ELEV(gobject);
	/* Free data */
	grits_wms_free(elev->wms);
	grits_tile_free(elev->tiles, NULL, elev);
	G_OBJECT_CLASS(grits_plugin_elev_parent_class)->finalize(gobject);

}
static void grits_plugin_elev_class_init(GritsPluginElevClass *klass)
{
	g_debug("GritsPluginElev: class_init");
	GObjectClass *gobject_class = (GObjectClass*)klass;
	gobject_class->dispose  = grits_plugin_elev_dispose;
	gobject_class->finalize = grits_plugin_elev_finalize;
}
