/*
 * Copyright (C) 2009, 2012 Andy Spencer <spenceal@rose-hulman.edu>
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


#include <config.h>
#include <stdio.h>
#include <math.h>
#include <glib.h>

#include "grits-tms.h"

static gchar *_make_uri(GritsTms *tms, GritsTile *tile)
{
	gint zoom = 0;
	for (GritsTile *tmp = tile->parent; tmp; tmp = tmp->parent)
		zoom++;
	gint breath = pow(2,zoom);

	gdouble lat_top = asinh(tan(deg2rad(tile->edge.n)));
	gdouble lat_bot = asinh(tan(deg2rad(tile->edge.s)));

	gdouble lat_mid = (lat_top + lat_bot)/2.0;
	gdouble lon_mid = (tile->edge.e + tile->edge.w)/2.0;

	gdouble lat_pos = 1.0 - (lat_mid + G_PI) / (2.0*G_PI);
	gdouble lon_pos = (lon_mid + 180.0) / 360.0;

	gint xtile = lon_pos * breath;
	gint ytile = lat_pos * breath;

	//g_message("tile=%f,%f,%f,%f t=%p p=%p",
	//		tile->edge.n, tile->edge.s,
	//		tile->edge.e, tile->edge.w, tile, tile->parent);
	//g_message("top=%lf->%lf bot=%lf->%lf pos=%lf,%lf tile=%d,%d,%d",
	//		tile->edge.n, lat_top,
	//		tile->edge.s, lat_bot,
	//		lat_pos, lon_pos,
	//		zoom, xtile, ytile);

	// http://tile.openstreetmap.org/<zoom>/<xtile>/<ytile>.png
	return g_strdup_printf("%s/%d/%d/%d.%s",
			tms->uri_prefix, zoom, xtile, ytile, tms->extension);
}

gchar *grits_tms_fetch(GritsTms *tms, GritsTile *tile, GritsCacheType mode,
		GritsChunkCallback callback, gpointer user_data)
{
	/* Get file path */
	gchar *uri   = _make_uri(tms, tile);
	gchar *tilep = grits_tile_get_path(tile);
	gchar *local = g_strdup_printf("%s%s", tilep, tms->extension);
	gchar *path  = grits_http_fetch(tms->http, uri, local,
			mode, callback, user_data);
	g_free(uri);
	g_free(tilep);
	g_free(local);
	return path;
}

GritsTms *grits_tms_new(const gchar *uri_prefix,
		const gchar *prefix, const gchar *extension)
{
	GritsTms *tms = g_new0(GritsTms, 1);
	tms->http         = grits_http_new(prefix);
	tms->uri_prefix   = g_strdup(uri_prefix);
	tms->extension    = g_strdup(extension);
	return tms;
}

void grits_tms_free(GritsTms *tms)
{
	grits_http_free(tms->http);
	g_free(tms->uri_prefix);
	g_free(tms->extension);
	g_free(tms);
}
