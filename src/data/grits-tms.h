/*
 * Copyright (C) 2009 Andy Spencer <spenceal@rose-hulman.edu>
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

#ifndef __GRITS_TMS_H__
#define __GRITS_TMS_H__

#include <glib.h>

#include "data/grits-http.h"
#include "objects/grits-tile.h"

typedef struct _GritsTms GritsTms;

struct _GritsTms {
	GritsHttp *http;
	gchar *uri_prefix;
	gchar *cache_prefix;
	gchar *extension;
};

gchar *grits_tms_fetch(GritsTms *tms, GritsTile *tile, GritsCacheType mode,
		GritsChunkCallback callback, gpointer user_data);

GritsTms *grits_tms_new(const gchar *uri_prefix, const gchar *cache_prefix, const gchar *extention);

void grits_tms_free(GritsTms *self);

#endif
