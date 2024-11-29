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
 * SECTION:map
 * @short_description: Map plugin
 *
 * #GritsPluginMap provides map overlays. Much of this data is obtained from the
 * OpenStreetMap project.
 */

#include <time.h>
#include <glib/gstdio.h>

#include <grits.h>

#include "map.h"

#define MAX_RESOLUTION 1
#define TILE_WIDTH     256
#define TILE_HEIGHT    256

//#define MAX_RESOLUTION 100
//#define TILE_WIDTH     1024
//#define TILE_HEIGHT    512

static const guchar colormap[][2][4] = {
	{{0x73, 0x91, 0xad}, {0x73, 0x91, 0xad, 0x00}}, // Oceans
	{{0xf6, 0xee, 0xee}, {0xf6, 0xee, 0xee, 0x00}}, // Ground
	{{0xff, 0xff, 0xff}, {0xff, 0xff, 0xff, 0xff}}, // Borders
	{{0x73, 0x93, 0xad}, {0x73, 0x93, 0xad, 0x40}}, // Lakes
	{{0xff, 0xe1, 0x80}, {0xff, 0xe1, 0x80, 0x60}}, // Cities
};

static void _load_tile_thread(gpointer _tile, gpointer _map)
{
	GritsTile      *tile = _tile;
	GritsPluginMap *map  = _map;

	g_debug("GritsPluginMap: _load_tile_thread start %p - tile=%p",
			g_thread_self(), tile);
	if (map->aborted) {
		g_debug("GritsPluginMap: _load_tile_thread - aborted");
		return;
	}

	/* Download tile */
	gchar *path = grits_tms_fetch(map->tms, tile, GRITS_ONCE, NULL, NULL);
	//gchar *path = grits_wms_fetch(map->wms, tile, GRITS_ONCE, NULL, NULL);
	if (!path) return; // Canceled/error

	/* Load pixbuf */
	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(path, NULL);
	if (!pixbuf) {
		g_warning("GritsPluginMap: _load_tile_thread - Error loading pixbuf %s", path);
		g_remove(path);
		g_free(path);
		return;
	}
	g_free(path);

#ifdef MAP_MAP_COLORS
	/* Map texture colors, if needed */
	gint    width  = gdk_pixbuf_get_width(pixbuf);
	gint    height = gdk_pixbuf_get_height(pixbuf);
	guchar *pixels = gdk_pixbuf_get_pixels(pixbuf);
	for (int i = 0; i < width * height; i++) {
		for (int j = 0; j < G_N_ELEMENTS(colormap); j++) {
			if (pixels[i*4+0] == colormap[j][0][0] &&
			    pixels[i*4+1] == colormap[j][0][1] &&
			    pixels[i*4+2] == colormap[j][0][2]) {
				pixels[i*4+0] = colormap[j][1][0];
				pixels[i*4+1] = colormap[j][1][1];
				pixels[i*4+2] = colormap[j][1][2];
				pixels[i*4+3] = colormap[j][1][3];
				break;
			}
		}
	}
#endif

	/* Load the GL texture from the main thread */
	grits_tile_load_pixbuf(tile, pixbuf);
	g_debug("GritsPluginMap: _load_tile_thread end %p", g_thread_self());
}

static void _load_tile_func(GritsTile *tile, gpointer _map)
{
	g_debug("GritsPluginMap: _load_tile_func - tile=%p", tile);
	GritsPluginMap *map = _map;
	g_thread_pool_push(map->threads, tile, NULL);
}

/*************
 * Callbacks *
 *************/
static void _on_location_changed(GritsViewer *viewer,
		gdouble lat, gdouble lon, gdouble elev, GritsPluginMap *map)
{
	GritsPoint eye = {lat, lon, elev};
	grits_tile_update(map->tiles, &eye,
			MAX_RESOLUTION, TILE_WIDTH, TILE_WIDTH,
			_load_tile_func, map);
	grits_tile_gc(map->tiles, time(NULL)-10, NULL, map);
}

/***********
 * Methods *
 ***********/
/**
 * grits_plugin_map_new:
 * @viewer: the #GritsViewer to use for drawing
 *
 * Create a new instance of the map plugin.
 *
 * Returns: the new #GritsPluginMap
 */
GritsPluginMap *grits_plugin_map_new(GritsViewer *viewer)
{
	g_debug("GritsPluginMap: new");
	GritsPluginMap *map = g_object_new(GRITS_TYPE_PLUGIN_MAP, NULL);
	map->viewer = g_object_ref(viewer);

	/* Load initial tiles */
	gdouble lat, lon, elev;
	grits_viewer_get_location(viewer, &lat, &lon, &elev);
	_on_location_changed(viewer, lat, lon, elev, map);

	/* Connect signals */
	map->sigid = g_signal_connect(map->viewer, "location-changed",
			G_CALLBACK(_on_location_changed), map);

	/* Add renderers */
	grits_viewer_add(viewer, GRITS_OBJECT(map->tiles), GRITS_LEVEL_WORLD, FALSE);

	return map;
}


/****************
 * GObject code *
 ****************/
/* Plugin init */
static void grits_plugin_map_plugin_init(GritsPluginInterface *iface);
G_DEFINE_TYPE_WITH_CODE(GritsPluginMap, grits_plugin_map, G_TYPE_OBJECT,
		G_IMPLEMENT_INTERFACE(GRITS_TYPE_PLUGIN,
			grits_plugin_map_plugin_init));
static void grits_plugin_map_plugin_init(GritsPluginInterface *iface)
{
	g_debug("GritsPluginMap: plugin_init");
	/* Add methods to the interface */
}
/* Class/Object init */
static void grits_plugin_map_init(GritsPluginMap *map)
{
	g_debug("GritsPluginMap: init");
	/* Set defaults */
	map->threads = g_thread_pool_new(_load_tile_thread, map, 1, FALSE, NULL);
	map->tiles = grits_tile_new(NULL, 85.0511, -85.0511, EAST, WEST);
	map->tms   = grits_tms_new("http://tile.openstreetmap.org",
		"osmtile/", "png");
	map->tiles->proj = GRITS_PROJ_MERCATOR;
	//map->tiles = grits_tile_new(NULL, NORTH, SOUTH, EAST, WEST);
	//map->wms   = grits_wms_new(
	//	"http://vmap0.tiles.osgeo.org/wms/vmap0",
	//	"basic,priroad,secroad,depthcontour,clabel,statelabel",
	//	 "image/png", "osm/", "png", TILE_WIDTH, TILE_HEIGHT);
	g_object_ref(map->tiles);
}
static void grits_plugin_map_dispose(GObject *gobject)
{
	g_debug("GritsPluginMap: dispose");
	GritsPluginMap *map = GRITS_PLUGIN_MAP(gobject);
	map->aborted = TRUE;
	/* Drop references */
	if (map->viewer) {
		GritsViewer *viewer = map->viewer;
		g_signal_handler_disconnect(viewer, map->sigid);
		grits_http_abort(map->tms->http);
		//grits_http_abort(map->wms->http);
		g_thread_pool_free(map->threads, TRUE, TRUE);
		map->viewer = NULL;
		grits_object_destroy_pointer(&map->tiles);
		g_object_unref(viewer);
	}
	G_OBJECT_CLASS(grits_plugin_map_parent_class)->dispose(gobject);
}
static void grits_plugin_map_finalize(GObject *gobject)
{
	g_debug("GritsPluginMap: finalize");
	GritsPluginMap *map = GRITS_PLUGIN_MAP(gobject);
	/* Free data */
	grits_tms_free(map->tms);
	//grits_wms_free(map->wms);
	grits_tile_free(map->tiles, NULL, map);
	G_OBJECT_CLASS(grits_plugin_map_parent_class)->finalize(gobject);

}
static void grits_plugin_map_class_init(GritsPluginMapClass *klass)
{
	g_debug("GritsPluginMap: class_init");
	GObjectClass *gobject_class = (GObjectClass*)klass;
	gobject_class->dispose  = grits_plugin_map_dispose;
	gobject_class->finalize = grits_plugin_map_finalize;
}
