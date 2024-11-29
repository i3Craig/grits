/*
 * Copyright (C) 2009-2010 Andy Spencer <andy753421@gmail.com>
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

#ifndef __GRITS_HTTP_H__
#define __GRITS_HTTP_H__

#include <glib.h>
#include <libsoup/soup.h>

#include "grits-data.h"

typedef struct _GritsHttp {
	SoupSession *soup;
	gchar *prefix;
	gboolean aborted;
} GritsHttp;

GritsHttp *grits_http_new(const gchar *prefix);

void grits_http_abort(GritsHttp *http);

void grits_http_free(GritsHttp *http);

gchar *grits_http_fetch(GritsHttp *http, const gchar *uri, const gchar *local,
		GritsCacheType mode,
		GritsChunkCallback callback,
		gpointer user_data);

GList *grits_http_available(GritsHttp *http,
		gchar *filter, gchar *cache,
		gchar *extract, gchar *index);

#endif
