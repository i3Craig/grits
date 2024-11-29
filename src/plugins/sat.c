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
 * SECTION:sat
 * @short_description: Satellite plugin
 *
 * #GritsPluginSat provides overlays using satellite imagery. This is mostly
 * provided by NASA's Blue Marble Next Generation.
 */

#include <time.h>
#include <string.h>
#include <glib/gstdio.h>

#include <grits.h>

#include "sat.h"

#define MAX_RESOLUTION 500
#define TILE_WIDTH     1024
#define TILE_HEIGHT    512

static void _load_tile_thread(gpointer _tile, gpointer _sat)
{
	GritsTile      *tile = _tile;
	GritsPluginSat *sat  = _sat;

	g_debug("GritsPluginSat: _load_tile_thread start %p - tile=%p",
			g_thread_self(), tile);
	if (sat->aborted) {
		g_debug("GritsPluginSat: _load_tile_thread - aborted");
		return;
	}

	/* Download tile */
	gchar *path = grits_wms_fetch(sat->wms, tile, GRITS_ONCE, NULL, NULL);
	if (!path) return; // Canceled/error

	/* Load pixbuf */
	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(path, NULL);
	if (!pixbuf) {
		g_warning("GritsPluginSat: _load_tile_thread - Error loading pixbuf %s", path);
		g_remove(path);
		g_free(path);
		return;
	}
	g_free(path);

	/* Draw a border */
#ifdef DRAW_TILE_BORDER
	gint    border = 10;
	gint    width  = gdk_pixbuf_get_width(pixbuf);
	gint    height = gdk_pixbuf_get_height(pixbuf);
	gint    stride = gdk_pixbuf_get_rowstride(pixbuf);
	guchar *pixels = gdk_pixbuf_get_pixels(pixbuf);
	for (int i = 0; i < border; i++) {
		memset(&pixels[(         i)*stride], 0xff, stride);
		memset(&pixels[(height-1-i)*stride], 0xff, stride);
	}
	for (int i = 0; i < height; i++) {
		memset(&pixels[(i*stride)                   ], 0xff, border*4);
		memset(&pixels[(i*stride)+((width-border)*4)], 0xff, border*4);
	}
#endif

	/* Load the GL texture from the main thread */
	grits_tile_load_pixbuf(tile, pixbuf);
	g_debug("GritsPluginSat: _load_tile_thread end %p", g_thread_self());
}

static void _load_tile_func(GritsTile *tile, gpointer _sat)
{
	g_debug("GritsPluginSat: __load_tile_func - tile=%p", tile);
	GritsPluginSat *sat = _sat;
	g_thread_pool_push(sat->threads, tile, NULL);
}

/*************
 * Callbacks *
 *************/
static void _on_location_changed(GritsViewer *viewer,
		gdouble lat, gdouble lon, gdouble elev, GritsPluginSat *sat)
{
	GritsPoint eye = {lat, lon, elev};
	grits_tile_update(sat->tiles, &eye,
			MAX_RESOLUTION, TILE_WIDTH, TILE_WIDTH,
			_load_tile_func, sat);
	grits_tile_gc(sat->tiles, time(NULL)-10, NULL, sat);
}

/***********
 * Methods *
 ***********/
/**
 * grits_plugin_sat_new:
 * @viewer: the #GritsViewer to use for drawing
 *
 * Create a new instance of the satellite plugin.
 *
 * Returns: the new #GritsPluginSat
 */
GritsPluginSat *grits_plugin_sat_new(GritsViewer *viewer)
{
	g_debug("GritsPluginSat: new");
	GritsPluginSat *sat = g_object_new(GRITS_TYPE_PLUGIN_SAT, NULL);
	sat->viewer = g_object_ref(viewer);

	/* Load initial tiles */
	gdouble lat, lon, elev;
	grits_viewer_get_location(viewer, &lat, &lon, &elev);
	_on_location_changed(viewer, lat, lon, elev, sat);

	/* Connect signals */
	sat->sigid = g_signal_connect(sat->viewer, "location-changed",
			G_CALLBACK(_on_location_changed), sat);

	/* Add renderers */
	grits_viewer_add(viewer, GRITS_OBJECT(sat->tiles), GRITS_LEVEL_WORLD, FALSE);

	return sat;
}


/****************
 * GObject code *
 ****************/
/* Plugin init */
static void grits_plugin_sat_plugin_init(GritsPluginInterface *iface);
G_DEFINE_TYPE_WITH_CODE(GritsPluginSat, grits_plugin_sat, G_TYPE_OBJECT,
		G_IMPLEMENT_INTERFACE(GRITS_TYPE_PLUGIN,
			grits_plugin_sat_plugin_init));
static void grits_plugin_sat_plugin_init(GritsPluginInterface *iface)
{
	g_debug("GritsPluginSat: plugin_init");
	/* Add methods to the interface */
}
/* Class/Object init */
static void grits_plugin_sat_init(GritsPluginSat *sat)
{
	g_debug("GritsPluginSat: init");
	/* Set defaults */
	sat->threads = g_thread_pool_new(_load_tile_thread, sat, 1, FALSE, NULL);
	sat->tiles = grits_tile_new(NULL, NORTH, SOUTH, EAST, WEST);
	sat->wms   = grits_wms_new(
		"http://www.nasa.network.com/wms", "bmng200406", "image/jpeg",
		"bmng/", "jpg", TILE_WIDTH, TILE_HEIGHT);
	g_object_ref(sat->tiles);
}
static void grits_plugin_sat_dispose(GObject *gobject)
{
	g_debug("GritsPluginSat: dispose");
	GritsPluginSat *sat = GRITS_PLUGIN_SAT(gobject);
	sat->aborted = TRUE;
	/* Drop references */
	if (sat->viewer) {
		GritsViewer *viewer = sat->viewer;
		g_signal_handler_disconnect(viewer, sat->sigid);
		grits_http_abort(sat->wms->http);
		g_thread_pool_free(sat->threads, TRUE, TRUE);
		sat->viewer = NULL;
		grits_object_destroy_pointer(&sat->tiles);
		g_object_unref(viewer);
	}
	G_OBJECT_CLASS(grits_plugin_sat_parent_class)->dispose(gobject);
}
static void grits_plugin_sat_finalize(GObject *gobject)
{
	g_debug("GritsPluginSat: finalize");
	GritsPluginSat *sat = GRITS_PLUGIN_SAT(gobject);
	/* Free data */
	grits_wms_free(sat->wms);
	grits_tile_free(sat->tiles, NULL, sat);
	G_OBJECT_CLASS(grits_plugin_sat_parent_class)->finalize(gobject);

}
static void grits_plugin_sat_class_init(GritsPluginSatClass *klass)
{
	g_debug("GritsPluginSat: class_init");
	GObjectClass *gobject_class = (GObjectClass*)klass;
	gobject_class->dispose  = grits_plugin_sat_dispose;
	gobject_class->finalize = grits_plugin_sat_finalize;
}
