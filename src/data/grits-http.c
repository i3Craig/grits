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
 * SECTION:grits-http
 * @short_description: Hyper Text Transfer Protocol
 *
 * #GritsHttp is a small wrapper around libsoup to provide data access using
 * the Hyper Text Transfer Protocol. Each #GritsHttp should be associated with
 * a particular server or dataset, all the files downloaded for this dataset
 * will be cached together in $HOME/.cache/grits/
 */

#include <config.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <libsoup/soup.h>

#include "grits-http.h"

gchar *_get_cache_path(GritsHttp *http, const gchar *local)
{
	return g_build_filename(g_get_user_cache_dir(), PACKAGE,
			http->prefix, local, NULL);
}

/**
 * grits_http_new:
 * @prefix: The prefix in the cache to store the downloaded files.
 *          For example: * "/nexrad/level2/".
 *
 * Create a new #GritsHttp for the given prefix
 *
 * Returns: the new #GritsHttp
 */
GritsHttp *grits_http_new(const gchar *prefix)
{
	g_debug("GritsHttp: new - %s", prefix);
	GritsHttp *http = g_new0(GritsHttp, 1);
	http->soup = soup_session_sync_new();
	http->prefix = g_strdup(prefix);
	g_object_set(http->soup, "user-agent", PACKAGE_STRING, NULL);
	g_object_set(http->soup, "timeout",    10,             NULL);
	return http;
}

/**
 * grits_http_abort:
 * @http: the #GritsHttp to abort
 *
 * Cancels any pending requests and prevents new requests.
 */
void grits_http_abort(GritsHttp *http)
{
	g_debug("GritsHttp: abort - %s", http->prefix);
	http->aborted = TRUE;
	soup_session_abort(http->soup);
}

/**
 * grits_http_free:
 * @http: the #GritsHttp to free
 *
 * Frees resources used by @http and cancels any pending requests.
 */
void grits_http_free(GritsHttp *http)
{
	g_debug("GritsHttp: free - %s", http->prefix);
	g_object_unref(http->soup);
	g_free(http->prefix);
	g_free(http);
}

/* For passing data to the chunck callback */
struct _CacheInfo {
	FILE  *fp;
	gchar *path;
	GritsChunkCallback callback;
	gpointer user_data;
};
struct _CacheInfoMain {
	gchar *path;
	GritsChunkCallback callback;
	gpointer user_data;
	goffset cur, total;
};

/* call the user callback from the main thread,
 * since it's usually UI updates */
static gboolean _chunk_main_cb(gpointer _infomain)
{
	struct _CacheInfoMain *infomain = _infomain;
	infomain->callback(infomain->path,
			infomain->cur, infomain->total,
			infomain->user_data);
	g_free(infomain);
	return FALSE;
}

/**
 * Append data to the file and call the users callback if they supplied one.
 */
static void _chunk_cb(SoupMessage *message, SoupBuffer *chunk, gpointer _info)
{
	struct _CacheInfo *info = _info;

	if (!SOUP_STATUS_IS_SUCCESSFUL(message->status_code)) {
		g_warning("GritsHttp: _chunk_cb - soup failed with %d",
				message->status_code);
		return;
	}

	if (!fwrite(chunk->data, chunk->length, 1, info->fp))
		g_error("GritsHttp: _chunk_cb - Unable to write data");

	if (info->callback) {
		struct _CacheInfoMain *infomain = g_new0(struct _CacheInfoMain, 1);
		infomain->path      = info->path;
		infomain->callback  = info->callback;
		infomain->user_data = info->user_data;
		infomain->cur       = ftell(info->fp);
		goffset st=0, end=0;
		soup_message_headers_get_content_range(message->response_headers,
				&st, &end, &infomain->total);
		g_idle_add(_chunk_main_cb, infomain);
	}

}

/**
 * grits_http_fetch:
 * @http:      the #GritsHttp connection to use
 * @uri:       the URI to fetch
 * @local:     the local name to give to the file
 * @mode:      the update type to use when fetching data
 * @callback:  callback to call when a chunk of data is received
 * @user_data: user data to pass to the callback
 *
 * Fetch a file from the cache. Whether the file is actually loaded from the
 * remote server depends on the value of @mode.
 *
 * Returns: The local path to the complete file
 */
/* TODO: use .part extentions and continue even when using GRITS_ONCE */
gchar *grits_http_fetch(GritsHttp *http, const gchar *uri, const char *local,
		GritsCacheType mode, GritsChunkCallback callback, gpointer user_data)
{
	g_debug("GritsHttp: fetch - %s mode=%d", local, mode);
	if (http->aborted) {
		g_debug("GritsPluginSat: _load_tile_thread - aborted");
		return NULL;
	}
	gchar *path = _get_cache_path(http, local);

	/* Unlink the file if we're refreshing it */
	if (mode == GRITS_REFRESH)
		g_remove(path);

	/* Do the cache if necessasairy */
	if (!(mode == GRITS_ONCE && g_file_test(path, G_FILE_TEST_EXISTS)) &&
			mode != GRITS_LOCAL) {
		g_debug("GritsHttp: fetch - Caching file %s", local);

		/* Open the file for writting */
		gchar *part = path;
		if (!g_file_test(path, G_FILE_TEST_EXISTS))
			part = g_strdup_printf("%s.part", path);
		FILE *fp = fopen_p(part, "ab");
		if (!fp) {
			g_warning("GritsHttp: fetch - error opening %s", path);
			return NULL;
		}
		fseek(fp, 0, SEEK_END); // "a" is broken on Windows, twice

		/* Make temp data */
		struct _CacheInfo info = {
			.fp        = fp,
			.path      = path,
			.callback  = callback,
			.user_data = user_data,
		};

		/* Download the file */
		SoupMessage *message = soup_message_new("GET", uri);
		if (message == NULL)
			g_error("message is null, cannot parse uri");
		g_signal_connect(message, "got-chunk", G_CALLBACK(_chunk_cb), &info);
		//if (ftell(fp) > 0)
			soup_message_headers_set_range(message->request_headers, ftell(fp), -1);
		if (mode == GRITS_REFRESH)
			soup_message_headers_replace(message->request_headers,
					"Cache-Control", "max-age=0");
		soup_session_send_message(http->soup, message);

		/* Close file */
		fclose(fp);
		if (path != part) {
			if (SOUP_STATUS_IS_SUCCESSFUL(message->status_code))
				g_rename(part, path);
			g_free(part);
		}

		/* Finished */
		guint status = message->status_code;
		g_object_unref(message);
		if (status == SOUP_STATUS_CANCELLED) {
			return NULL;
		} else if (status == SOUP_STATUS_REQUESTED_RANGE_NOT_SATISFIABLE) {
			/* Range unsatisfiable, file already complete */
		} else if (!SOUP_STATUS_IS_SUCCESSFUL(status)) {
			g_warning("GritsHttp: done_cb - error copying file, status=%d\n"
					"\tsrc=%s\n"
					"\tdst=%s",
					status, uri, path);
			return NULL;
		}
	}

	/* TODO: free everything.. */
	return path;
}

/**
 * grits_http_available:
 * @http:    the #GritsHttp connection to use
 * @filter:  filter used to extract files from the index, or NULL
 *           For example: "href=\"([^"]*)\""
 * @cache:   path to the local cache, or NULL to not search the cache
 * @extract: regex used to extract filenames from the page, should match the
 *           filename as $1, or NULL to use /http="([^"])"/
 * @index:   path to the index page, or NULL to not search online
 *
 * Look through the cache and an HTTP index page for a list of available files.
 * The name of each file that matches the filter is added to the returned list.
 *
 * The list as well as the strings contained in it should be freed afterwards.
 *
 * Returns the list of matching filenames
 */
GList *grits_http_available(GritsHttp *http,
		gchar *filter, gchar *cache,
		gchar *extract, gchar *index)
{
	g_debug("GritsHttp: available - %s~=%s %s~=%s",
			filter, cache, extract, index);
	GRegex *filter_re = g_regex_new(filter, 0, 0, NULL);
	GList  *files = NULL;

	/* Add cached files */
	if (cache) {
		const gchar *file;
		gchar *path = _get_cache_path(http, cache);
		GDir  *dir  = g_dir_open(path, 0, NULL);
		while (dir && (file = g_dir_read_name(dir)))
			if (g_regex_match(filter_re, file, 0, NULL))
				files = g_list_prepend(files, g_strdup(file));
		g_free(path);
		if (dir)
			g_dir_close(dir);
	}

	/* Add online files if online */
	if (index) {
		gchar tmp[32];
		g_snprintf(tmp, sizeof(tmp), ".index.%x", g_random_int());
		gchar *path = grits_http_fetch(http, index, tmp,
				GRITS_REFRESH, NULL, NULL);
		if (!path)
			return files;
		gchar *html;
		g_file_get_contents(path, &html, NULL, NULL);
		if (!html)
			return files;

		/* Match hrefs by default, this regex is not very accurate */
		GRegex *extract_re = g_regex_new(
				extract ?: "href=\"([^\"]*)\"", 0, 0, NULL);
		GMatchInfo *info;
		g_regex_match(extract_re, html, 0, &info);
		while (g_match_info_matches(info)) {
			gchar *file = g_match_info_fetch(info, 1);
			if (file) {
				if (g_regex_match(filter_re, file, 0, NULL))
					files = g_list_prepend(files, file);
				else
					g_free(file);
			}
			g_match_info_next(info, NULL);
		}

		g_regex_unref(extract_re);
		g_match_info_free(info);
		g_unlink(path);
		g_free(path);
		g_free(html);
	}

	g_regex_unref(filter_re);

	return files;
}
