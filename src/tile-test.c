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

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "grits-util.h"
#include "data/grits-wms.h"
#include "data/grits-tms.h"
#include "objects/grits-tile.h"

#include "compat.h"

struct CacheState {
	GtkWidget *image;
	GtkWidget *status;
	GtkWidget *progress;
};

struct LoadData {
	GtkImage  *image;
	GdkPixbuf *pixbuf;
};

void chunk_callback(gsize cur, gsize total, gpointer _state)
{
	struct CacheState *state = _state;
	g_message("chunk_callback: %ld/%ld", (glong)cur, (glong)total);

	if (state->progress == NULL) {
		state->progress = gtk_progress_bar_new();
		gtk_box_pack_end(GTK_BOX(state->status), state->progress, FALSE, FALSE, 0);
		gtk_widget_show(state->progress);
	}

	if (cur == total)
		gtk_widget_destroy(state->progress);
	else
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(state->progress), (gdouble)cur/total);
}

gboolean load_callback(gpointer _data)
{
	struct LoadData *data = _data;
	gtk_image_set_from_pixbuf(GTK_IMAGE(data->image), data->pixbuf);
	g_free(data);
	return FALSE;
}

void load_image(GtkImage *image, GdkPixbuf *pixbuf)
{
	struct LoadData *data = g_new0(struct LoadData, 1);
	data->image  = image;
	data->pixbuf = pixbuf;
	g_idle_add(load_callback, data);
}

gpointer do_bmng_cache(gpointer _image)
{
	GtkImage *image = _image;
	g_message("Creating bmng tile");
	GritsTile *tile = grits_tile_new(NULL, NORTH, SOUTH, EAST, WEST);
	tile->children[0][1] = grits_tile_new(tile, NORTH, 0, 0, WEST);
	tile = tile->children[0][1];

	g_message("Fetching bmng image");
	GritsWms *bmng_wms = grits_wms_new(
		"http://www.nasa.network.com/wms", "bmng200406", "image/jpeg",
		"bmng_test/", "jpg", 512, 256);
	const char *path = grits_wms_fetch(bmng_wms, tile, GRITS_ONCE, NULL, NULL);

	g_message("Loading bmng image: [%s]", path);
	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(path, NULL);
	load_image(GTK_IMAGE(image), pixbuf);

	g_message("Cleaning bmng up");
	grits_wms_free(bmng_wms);
	grits_tile_free(tile, NULL, NULL);
	return NULL;
}

gpointer do_osm_cache(gpointer _image)
{
	GtkImage *image = _image;
	g_message("Creating osm tile");
	GritsTile *tile = grits_tile_new(NULL, NORTH, SOUTH, EAST, WEST);
	tile->children[0][1] = grits_tile_new(tile, NORTH, 0, 0, WEST);
	tile = tile->children[0][1];

	g_message("Fetching osm image");
	GritsWms *osm_wms = grits_wms_new(
		"http://labs.metacarta.com/wms/vmap0", "basic", "image/png",
		"osm_test/", "png", 512, 256);
	const char *path = grits_wms_fetch(osm_wms, tile, GRITS_ONCE, NULL, NULL);

	g_message("Loading osm image: [%s]", path);
	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(path, NULL);
	load_image(GTK_IMAGE(image), pixbuf);

	g_message("Cleaning osm up");
	grits_wms_free(osm_wms);
	grits_tile_free(tile, NULL, NULL);
	return NULL;
}

gpointer do_osm2_cache(gpointer _image)
{
	GtkImage *image = _image;
	g_message("Creating osm2 tile");
	GritsTile *tile = grits_tile_new(NULL, 85.0511, -85.0511, EAST, WEST);
	tile->children[0][1] = grits_tile_new(tile, 85.0511, 0, 0, WEST);
	tile = tile->children[0][1];

	g_message("Fetching osm2 image");
	GritsTms *osm2_tms = grits_tms_new("http://tile.openstreetmap.org",
			"tms_test/", "png");
	const char *path = grits_tms_fetch(osm2_tms, tile, GRITS_ONCE, NULL, NULL);

	g_message("Loading osm2 image: [%s]", path);
	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(path, NULL);
	load_image(GTK_IMAGE(image), pixbuf);

	g_message("Cleaning osm2 up");
	grits_tms_free(osm2_tms);
	grits_tile_free(tile, NULL, NULL);
	return NULL;
}


gboolean key_press_cb(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
	if (event->keyval == GDK_KEY_q)
		gtk_main_quit();
	return TRUE;
}

int main(int argc, char **argv)
{
	gtk_init(&argc, &argv);

	GtkWidget *win        = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	GtkWidget *vbox1      = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget *vbox2      = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget *status     = gtk_statusbar_new();
	GtkWidget *scroll     = gtk_scrolled_window_new(NULL, NULL);
	GtkWidget *bmng_image = gtk_image_new();
	GtkWidget *srtm_image = gtk_image_new();
	GtkWidget *osm_image  = gtk_image_new();
	GtkWidget *osm2_image = gtk_image_new();
	gtk_container_add(GTK_CONTAINER(win), vbox1);
	gtk_box_pack_start(GTK_BOX(vbox1), scroll, TRUE, TRUE, 0);
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scroll), vbox2);
	gtk_box_pack_start(GTK_BOX(vbox2), bmng_image, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox2), srtm_image, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox2), osm_image,  TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox2), osm2_image, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox1), status, FALSE, FALSE, 0);
	g_signal_connect(win, "key-press-event", G_CALLBACK(key_press_cb), NULL);
	g_signal_connect(win, "destroy", gtk_main_quit, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
			GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	g_thread_new("bmng-thread", do_bmng_cache, bmng_image);
	g_thread_new("osm-thread",  do_osm_cache,  osm_image);
	g_thread_new("osm2-thread", do_osm2_cache, osm2_image);

	gtk_widget_show_all(win);
	gtk_main();

	return 0;
}
